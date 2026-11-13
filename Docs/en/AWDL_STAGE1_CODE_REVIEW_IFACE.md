# Code review — `AWDLInterface.cpp` / `AWDLInterface.h` (stage 1)

Verification note: finding 1 was applied (`rtl_vif_start` declared `bool`). Finding 2 is closed by `AWDL_STAGE1_PENDING_CHECKS.md` §3 (the interface's `free` already deregisters; the explicit call is not necessary). Finding 6 does not apply: `AWDL_STAGE1_LOG_STRINGS_AUDIT.md` confirms the real literal is `awdl: registerEthernetInterface 0x%x` and the test guide looks for it that way. Finding 3 (vtable cell order) is closed: the dump of `__ZTV23IO80211VirtualInterface` in the kernelcache matches cell by cell the header in the 27 methods (`0xE68`-`0xF38`), pure `setMacAddress` (`0xF40`) and the 16 reserved ones (`0xF48`-`0xFC0`), and the 63 slots `0xFC8`-`0x11B8` of `__ZTV30AppleBCMWLANProximityInterface` match by name and position the 63 declared ones (0 discrepancies).

Line-by-line review of `Source/AirLayer/AWDLInterface.cpp` (311 lines) and
`Source/AirLayer/AWDLInterface.h` (412 lines) against
[`AWDL_STAGE1_IMPL_SPEC.md`](AWDL_STAGE1_IMPL_SPEC.md) and
[`AWDL_STAGE1_CODE_FIXES.md`](AWDL_STAGE1_CODE_FIXES.md). No code or build file has been
modified. IDA was not used: the ABI contrast was done against the symbol dumps of
`Docs/re/io80211_25.6.0/` and against the already built object
(`Build/obj_airlayer/AWDLInterface.o`, fresh: same date as the `.cpp`; the `.h` is older).

Starting state verified in the tree: defects 1 (no-op action), 2 (rollback), 3
(`rtl_vif_init` bool) and 6 (headroom `+0x3C`) of CODE_FIXES are applied; 4
(`setAwdlInterface`) is not applied (decision documented in the handoff, "harmless"); 5
required no code change.

## 0. Method and starting verification

| Check | Result |
|---|---|
| `nm -u` of the built object | 543 undefineds; **93** from `IO80211VirtualInterface` — matches CODE_FIXES §0 |
| The 93, one by one, in `syms.txt` + `syms_com.apple.iokit.IOSkywalkFamily.txt` | **93/93 with type `0f`** |
| The 13 `extern` `__asm` of `AWDLInterface.cpp` against the dumps | 13/13 with exact spelling and type `0f` |
| Audit of the 543 undefineds containing `IO80211`/`IOSkywalk` | **0 without `0f`** |
| `__ZTI*`/`__ZTV*` undefineds | 0 (the build goes with `-fno-rtti`; there are no unresolved RTTI references) |
| Disassembly of the object's `free`/`stop`/`startWithRegistration` | see §3.1 and §2.2 |

The Skywalk symbols are not in `syms.txt` (kernel fileset): there
`registerEthernetInterface` appears with type `01` and null address (kernel undefined
reference). All are `0f` in the dump of the `IOSkywalkFamily` fileset; the addresses match
those cited in spec §5.1 and CODE_FIXES (e.g. `initRegistrationInfo` `0xffffff8002b108e2`,
`registerEthernetInterface` `0xffffff8002b10a84`, TX submission `withPool`
`0xffffff8002b0ca86`).

## 1. What was reviewed without defect

To bound the final table, this is what was contrasted and **does** not produce a finding:

- **Mangled names of the `extern`s** (`AWDLInterface.cpp:27-61`): the 13 match character by
  character. Two neighboring overloads are correctly avoided: `enqueuePackets` exists as
  `EPKP15IOSkywalkPacketjj` (`0xffffff8002b2dbb6`, the used one) and as `EP15IOSkywalkPacketjj`
  (`0xffffff8002b2dc6e`, unused); `registerEthernetInterface` exists as
  `PP20IOSkywalkPacketQueue` (used) and as `P20IOSkywalkLogicalLink` (unused). The
  `withPool` TX submission action is `unsigned int (*)(OSObject*, IOSkywalkTxSubmissionQueue*,
  IOSkywalkPacket* const*, unsigned int, void*)` per the mangling — the `txNoop` signature matches.
- **Vtable**: 50 overrides (`:96-145` + `free`/`stop`), 27 methods (`0xE68`-`0xF38`),
  pure `setMacAddress` (`0xF40`), 16 reserved (`0xF48`-`0xFC0`), 63 AWDL slots
  (`0xFC8`-`0x11B8`). Correct counts; the 63 stubs of `RTLAwdInterface` cover the 63 slots and
  `static_assert(!__is_abstract(RTLAwdInterface))` (`:406`) would fail if one were missing. The
  names of the 27 + 16 reserved are verified against the dump with their real types, including
  `12setLinkStateE16IO80211LinkStatej` (the enum, as in `:150`), the two overloads of
  `p2pMulticastTx` in the order of `:175-176` and the spelling `33_/34__RESERVED…` of the reserved ones.
- **Mechanism of the parent calls**: the object references neither `IO80211VirtualInterface::free`
  nor `::stop`. With `-fapple-kext`, the qualified call to the parent is emitted as a load of the
  exported family vtable's cell and an indirect call: in `RTLAwdInterface::free`,
  `callq *(ZTV of IO80211VirtualInterface + 0xA0)` — the cell of `IOService::free`, which holds the
  family's implementation. There is no dynamic dispatch (infinite recursion ruled out) nor
  unresolved symbol. The mechanism is correct in both cases (whether the family overwrites `free` or
  inherits `IOService::free`).
- **Metaclass**: `10gMetaClassE`, `10superClassE`, `9MetaClassC1/C2`, `C2EPK11OSMetaClass`,
  `D0/D1/D2`, `nwEm`, `dlEPvm` and `__ZTV19IO80211AWDLProtocol` — all `0f` (spec §5.1).
- **`RegInfo` offsets** (`:78-89`, `:181-191`): all per spec §5.3 — `+0x0C`=3,
  `+0x10`=6, `+0x14`=2, `+0x24`/`+0x28`=0x80, `+0x30`="awdl" (literal with permanent
  storage), `+0x38`=0, **`+0x3C`=64 u16** (defect 6 applied; the comment "+0x38 is the BSD
  unit" is correct), `+0x40`=0, MTU `0x594` at `+0x48` and `+0x4C` (the u64 write
  `0x59400000594` is correct in little-endian), MAC 6 B at `+0x108`, size `0x130`.
- **`txNoop`** (`:247-261`): batches of 64 < capacity 128, return `(IOReturn)count` (full
  consumption; 0 would stop the queue per `legacyDequeue`), on success the packets are completed by
  `enqueuePackets` → `completePackets` toward the pool; on failure it frees the batch with
  `deallocatePacket` on the correct pool (`_txPool`, the same one as the TX submission queue and the
  TX completion queue). It is literally CODE_FIXES §1.2, and strictly better than the STA
  path (`RTLAirLayer.cpp:433-435`), which ignores `enqueuePackets`'s return.
- **Rollback and lifecycle**: `teardownDatapath` on the two failures after
  `createDatapath` (defect 2 applied, `:201` and `:213`); `removeQueues` is idempotent
  (disables → removes from the work loop → frees → nulls, skipping nulls, `:223-239`), so
  later `stop()`/`free()` neither repeat work nor double-free; `createDatapath` failures prior to
  `addEventSource` leave the queues out of the work loop and `free()`
  (`:278-290`) frees them without dangling pointers; `_regInfo` is freed with `IOFree` of the same size as
  the `IOMalloc` and nulled. No path with a leak nor double-free found,
  assuming the handler's rollback (`terminate()` + `release()` in the patch, present).
- **Shared state**: the queue pointers are written before `addEventSource` and then only
  touched on the work loop (`txNoop`, `removeQueues` via `runAction`) or when they can no longer be
  on it (prior failures, `stop`/`free` after termination). The work loop's gate is recursive
  (SDK header, `IOWorkLoop.h:301`), so `runAction` from the work loop's thread does not
  self-block; the expected action is a function pointer
  `IOReturn (*)(OSObject*, void*, void*, void*, void*)` (`IOWorkLoop.h:78`) — the
  `removeQueues` signature matches. `_bsdName` is written under `_awdlLock` (patch) and read under the same
  lock. `enable`/`disable` (`:294-304`) touch no state, as spec §5.6 asks.
- **Boot sequence** (`:96-218`): steps 1-10 of spec §5.5 complete and in order (`init()` →
  `VI::init(ctrl, ea, 6, "awdl")` → `setInterfaceId(2)` → `attach` → `VI::start` → RegInfo →
  pool+3 queues → `registerEthernetInterface` → `registerService(kIOServiceSynchronous)` →
  `setRunningState(false)` → `getBSDName` with failure if empty). The `!name || !name[0]` guard before
  the `strlcpy` (`:211-216`) is an improvement over skeleton §12.2, which would copy
  `getBSDName()` without checking and would do `strlcpy(dst, NULL)` if the name did not exist.
- **`LOG`** (`:23-24`): correct. `airLog` adds the `RTL8814AirLayer: ` prefix and the
  `\n` itself (`RTLAirLayer.cpp:38`); the spec's skeleton (`"RTL8814AirLayer: " fmt "\n"`) would
  have duplicated the prefix. The patch's `rtlAirLog` is in the tree (`RTLAirLayer.cpp:61-70`).
- **94 payload and asserts**: 32 B `apple80211_virt_if_create_data` with `static_assert`
  (`:74-81`), layout of spec §4.1; size asserts `0x128`/`0x130`/`0x130` (`:287-289`).
- **`setMacAddress`** (`:306`) accepts without changing the MAC and the 63 stubs return
  `kIOReturnUnsupported` (102) — exactly spec §5.6 and §16 (D6 included: the companion 293
  answers 102).

## 2. Findings

### 2.1 `rtl_vif_start` declared `uintptr_t`; the real function returns `bool`

`Source/AirLayer/AWDLInterface.cpp:54-55`, called at `:168`. Severity: **incorrect
behavior (potential, `?` on the practical probability)**.

`IO80211VirtualInterface::start` is an override of `IOService::start`, which is `virtual bool`; the
its own header declares it `virtual bool start(IOService*) override;` (`AWDLInterface.h:98`). The
return type is not part of the mangling, so the symbol links the same, but the ABI only
guarantees bit `al` for a bool return; the rest of `rax` is unspecified. The object compiles the check as a test of all 64 bits:

```asm
000000000000090e	callq	__ZN23IO80211VirtualInterface5startEP9IOService
0000000000000913	cmpq	$0x0, %rax
0000000000000917	jne	0x933
```

If any branch of the family's implementation left garbage in the high bits of `rax`, the
boot would fail spuriously (`awdl: base start failed`, the 94 answers `0xE00002BC` and there is
no `awdl0`), without panic. Skeleton §12.1's note ("`uintptr_t` is used… they are not called from the
kext in stage 1") does not cover this case: this `extern` **is** called, and it is the only one in the file
in that situation next to `rtl_vif_init` (which is indeed correctly declared `bool`, defect 3 applied).

Before (`:54-55`):

```cpp
uintptr_t rtl_vif_start(void *self, IOService *provider)
	__asm("__ZN23IO80211VirtualInterface5startEP9IOService");
```

After:

```cpp
bool rtl_vif_start(void *self, IOService *provider)
	__asm("__ZN23IO80211VirtualInterface5startEP9IOService");
```

The change does not alter the symbol nor the convention; `:168` needs no edit. The practical
probability is low (`?`: the family's implementations usually return bool with `rax`
zero-extended, but it is not verifiable without IDA), and the failure would be safe (no `awdl0`, no
corruption).

### 2.2 Missing `deregisterEthernetInterface`/`detach` on BSD name failure

`Source/AirLayer/AWDLInterface.cpp:210-215`. Severity: **incorrect behavior, only on an
error path**; known deviation from spec §11 (CODE_FIXES §2.2 leaves it out with `?`).

By this point `registerService(kIOServiceSynchronous)` already finished and, if it got here, the ethernet
registration may have seeded state into the family (queues and pools handed over in step 8). The
code's rollback is `teardownDatapath()` —removes the queues from the work loop and frees them— but does not
deregister the ethernet interface nor do `detach`; spec §11 asks to "remove queues from the work
queue, `deregisterEthernetInterface` (if it got registered), detach, release". The handler does
`terminate()` + `release()`, which will fire `stop()` (idempotent) and `free()`, but `?` it is not
verified that termination by itself undoes the ethernet registration nor that it releases the
family's internal references to already freed queues: if the family retains them without a
`deregister`, the object dies with dangling pointers in the registered state.

Before (`:211-215`):

```cpp
	if (!name || !name[0]) {
		LOG("awdl: no bsd name");
		teardownDatapath();
		return false;
	}
```

After (minimal form; `?` the argument of `deregisterEthernetInterface` —the BSD unit— is not
fixed in the documents, and the `0f` symbol is at `0xffffff8002b10af4`):

```cpp
	if (!name || !name[0]) {
		LOG("awdl: no bsd name");
		teardownDatapath();
		/* deregisterEthernetInterface(unit) if the registration happened */
		return false;
	}
```

CODE_FIXES §8 already lists this point as open; it is kept as a finding because it is the only
real deviation from §11's rollback left in the file. In practice the path requires a
strange failure (successful synchronous registration with an empty name), so it does not block.

### 2.3 `rtl_sky_initRegInfo` declared `void`; the skeleton declares it `bool` and §11 expects rollback

`Source/AirLayer/AWDLInterface.cpp:28-29`, called at `:180`. Severity: **cosmetic** (with `?` on
the real type).

Skeleton §12.1 declares `bool rtl_sky_initRegInfo(...)`; the code declares it `void` and comments
that the reference path does not look at the result either. The real type is not decidable from the
dumps (`?` the return type is not in the mangling). Declaring `void` and not reading the return is
safe in any case (the problem would be the inverse: declaring `bool` over a `void` function
and reading it). Spec §11 has a rollback row for "`initRegistrationInfo` fails" that the code
—and the skeleton §12.2 itself— does not implement: the values passed (1, 0x130) are the valid
ones per §5.3, so failure is not expected.

Before (`:28`):

```cpp
void rtl_sky_initRegInfo(void *iface, void *reg, uint32_t version, unsigned long size)
```

After (alignment with the skeleton; no behavior change while the result is not
read):

```cpp
bool rtl_sky_initRegInfo(void *iface, void *reg, uint32_t version, unsigned long size)
```

### 2.4 `setAwdlInterface`/`rtlAwdlPublish` remain (defect 4 not applied)

`Source/AirLayer/AWDLInterface.cpp:56-57` and `:308-311`; `AWDLInterface.h:408-410`. Severity:
**cosmetic**.

CODE_FIXES §4 asks to remove them (0 references in the kernelcache; the 97 reads
`getPrimarySkywalkInterface`, not `getAwdlInterface`) and spec §7.2 withdrew the requirement. The
decision documented in the handoff is not to apply defect 4 ("harmless"), so it is not a
new defect but a known, deliberate deviation from the current spec. Minor added note:
the return is declared `IOReturn` and the family method's real type is not in the
dumps (`?`); the patch ignores the returned value in its two calls, so a real `void`
return would be harmless.

### 2.5 Registration log literal different from what T3.4 expects

`Source/AirLayer/AWDLInterface.cpp:199`. Severity: **cosmetic**.

The code prints `awdl: registerEthernetInterface 0x%x`; test T3.4 (spec §14) expects the
literal `awdl: registerEthernetInterface = 0x0` (with `=`, from skeleton §12.2). A `log show`
filtered by T3.4's exact string would not find the line. Unifying one of the two parts suffices.

Before (`:199`):

```cpp
	LOG("awdl: registerEthernetInterface 0x%x", r);
```

After:

```cpp
	LOG("awdl: registerEthernetInterface = 0x%x", r);
```

### 2.6 Defensive branches of `txNoop` returning 0 leave the queue stalled

`Source/AirLayer/AWDLInterface.cpp:251-252`. Severity: **cosmetic** (unreachable branch).

```cpp
	if (!me || !count || !me->_txCompQ)
		return 0;
```

Per `legacyDequeue` (CODE_FIXES §1.1), returning 0 sets the stop bit and leaves the batch in the
ring without advancing: the packets are never completed and the queue stalls. The three
conditions are unreachable in the current flow: `owner` is always `this` (the one passed to
`withPool`), the action is only invoked with pending packets (`count > 0`) and `_txCompQ` is only
nulled in `removeQueues` **after** disabling, removing and freeing `_txSubQ` — the action
cannot fire in that state. It is also the literal text of CODE_FIXES §1.2, so there is no
spec deviation; it is documented so a future edit of the action does not inherit the
pattern without seeing it. No change proposed (returning `count` on those branches would be worse: it would
claim consuming a batch that was not completed).

### 2.7 `initWithController` without null guards; ivar `_ctrl` without reader

`Source/AirLayer/AWDLInterface.cpp:96-103`. Severity: **cosmetic**.

`initWithController(RTLAirController *ctrl, const uint8_t mac[6])` does `memcpy(_mac, mac, 6)`
(`:103`) and uses `ctrl` in `rtl_vif_init`/`attach` without checking for nulls. The only caller is the
94's handler, which already validated `req` and passes `this`, so the guard would be defensive. The ivar
`_ctrl` (`:389`) is assigned and nobody reads it in this file: either remove it or keep it as an anchor for
stage 2. No change proposed.

### 2.8 Assumed all-or-nothing of `enqueuePackets` on the failed batch

`Source/AirLayer/AWDLInterface.cpp:256-258`. Severity: **none evidenced; verification
pending (`?`)**.

The failure branch frees the whole batch to the pool:

```cpp
		if (rtl_txcomp_enqueue(me->_txCompQ, pkts + done, n, 0) != kIOReturnSuccess)
			for (uint32_t i = 0; i < n; i++)
				rtl_pool_free(me->_txPool, pkts[done + i]);
```

The evidence of CODE_FIXES §1.1 describes an `enqueuePackets` that validates `0 < count < capacity`
before touching anything and, on success, copies the batch and completes it under its gate — i.e.
all-or-nothing. If a partial failure existed (it accepts part and returns error), the free
would return already-completed packets to the pool and corrupt the pool's count. With batches of
64 < 128 the validation always passes, so the documented failures (disabled queue) are
full rejections. Marks `?`: re-verify in the disassembly if the action is ever
reused with batches near capacity.

## 3. Pending points the review cannot close without IDA

- **Cell order of the vtable above `0xE60`** (`?`, potentially "blocks the
  boot" if wrong, unverified): the object verifies **names** (93/93 `0f`) but not the
  **order** of the 27 methods (`0xE68`-`0xF38`), `setMacAddress` (`0xF40`), the 16 reserved and
  the 63 AWDL. If the family's real order differed, `RTLAwdInterface`'s vtable cells would land shifted and the
  family's calls would hit the wrong method. It is exactly spec §14's check P0.3 (§5.2, §16), still not executed:
  neither `vt_IO80211VirtualInterface.txt` nor `vt_IO80211AWDLProtocol.txt` exists in
  `Docs/re/io80211_25.6.0/`. Qualifications bounding the risk: the 50 overrides are protected by
  `override` at compile time; and an internal permutation of the 63 AWDL stubs would be neutral in
  stage 1 (all 63 return the same 102), though a shift of the `0xE68`-`0xFC0`
  stretch would not be. Running P0.3 remains a precondition to loading the kext.
- **Tolerance of `IO80211VirtualInterface::free` to a half-done base `init`** (`?`, CODE_FIXES
  §3.2): applies to the `attach` failure at `:119-122`.
- **Reader of `+0x3C`** (`?`, CODE_FIXES §6.1): the 64 u16 write at `+0x3C` follows the reference path
  `createProxInterface`; the consumer is not identified.
- **`checkForWork` route of the TX completion queue** (`?`, CODE_FIXES §1.2): no known route leaving
  pending items; the no-op completes inside `enqueuePackets`.
- **Family's retention after registration** (`?`): conditions the real gravity of §2.2.
- **Real return of `setAwdlInterface` and of `initRegistrationInfo`** (`?`): they do not alter the
  current behavior (§2.3, §2.4).

Observation outside the scope of these two files: `build_airlayer.sh:40-41` compiles
`AWDLInterface.cpp` twice (duplicated line in the integration); it is harmless (the second
object overwrites the first) but worth cleaning in its flow.

## 4. Final table

| # | Finding | Where | Severity | Action |
|---:|---|---|---|---|
| 1 | `rtl_vif_start` `uintptr_t` over a `bool` return; the object tests all 64 bits of `rax` (`cmpq $0x0, %rax`) | `AWDLInterface.cpp:54-55`, `:168` | incorrect behavior (potential, `?`) | declare `bool` (1 line) |
| 2 | No `deregisterEthernetInterface`/`detach` on name failure; rollback incomplete vs spec §11 | `AWDLInterface.cpp:210-215` | incorrect behavior (error path only; documented in CODE_FIXES §2.2) | add the deregistration; `?` unit argument unfixed |
| 3 | Cell order after `0xE60` unchecked against the real vtable (P0.3 pending; no `vt_` dump of the classes) | `AWDLInterface.h:149-284` | blocks boot **if** wrong (`?` unverified) | run P0.3 before loading |
| 4 | `rtl_sky_initRegInfo` `void` vs the skeleton's `bool`; result ignored | `AWDLInterface.cpp:28-29`, `:180` | cosmetic | align to the skeleton (optional) |
| 5 | `setAwdlInterface`/`rtlAwdlPublish` kept against CODE_FIXES §4 / spec §7.2 | `AWDLInterface.cpp:56-57`, `:308-311`; `.h:408-410` | cosmetic (documented decision) | no action |
| 6 | Registration log without the `=` T3.4 expects | `AWDLInterface.cpp:199` | cosmetic | unify literal |
| 7 | Defensive branches of `txNoop` with `return 0` (would stall the queue; unreachable) | `AWDLInterface.cpp:251-252` | cosmetic | no action (documented) |
| 8 | No null guards in `initWithController`; `_ctrl` without reader | `AWDLInterface.cpp:96-103`, `.h:389` | cosmetic | optional |
| 9 | `enqueuePackets`' all-or-nothing assumed in the failed batch free | `AWDLInterface.cpp:256-258` | verification `?` (today: no evidence of partial failure) | re-verify if usage changes |

No confirmed finding blocks boot with the current tree; #3 is the pending condition that could, and #1 is the only active code defect with potential
effect in the normal flow.
