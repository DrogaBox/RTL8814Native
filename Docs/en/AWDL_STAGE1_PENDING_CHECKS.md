# Stage 1 pending checks

> Correction: the tx data offset store of `0xffffff8002affa94` goes to the stack `ifnet_init_eparams` and not to the BSD client; the value ends up at `ifnet+0x58C`. See `AWDL_STAGE1_LIFECYCLE_CHECKS.md` §1 and §4.

Closure of four open points of `AWDL_STAGE1_CODE_FIXES.md` and `AWDL_STAGE1_SPEC_DELTA.md`,
verified in the disassembly of the macOS 26.7.1 (25G241, x86_64) kernelcache over a copy of
the annotated base. Function addresses were checked one by one: every VA cited as a function is
a function start (the start matches the cited address); instruction VAs are cited inside
the verified function whose head is indicated. The points the disassembly does not fix carry `?`.

## 1. Reader of `RegistrationInfo+0x3C` on the copy

`AppleBCMWLANCore::createProxInterface` (head `0xffffff800165dcbe`) writes `word [r12+3Ch] = 40h`
(64, headroom) at `0xffffff800165de8c`, over the 0x130-byte `RegistrationInfo` it zeroes with
`bzero` at `0xffffff800165dda7` and passes to `AppleBCMWLANProximityInterface::start` at
`0xffffff800165dead`. The family makes two copies of that structure:

| VA | Fact |
|---|---|
| `0xffffff8002b109f0` | Head of `IOSkywalkEthernetInterface::copyRegistrationInfo`. Requires version 1 and size ≥ 0x130 (`0xffffff8002b10a08`-`0x10a15`), allocates with the typed allocator, saves the block at `[[this+0x118]]` (`0xffffff8002b10a26`-`0x10a2d`) and copies 0x130 bytes (`0xffffff8002b10a40`). |
| `0xffffff8002b10a84` / `0xffffff8002b10b46` | Heads of the two `registerEthernetInterface` overloads; they pass the copy of `+0x118` to `registerNetworkInterface`. |
| `0xffffff8002b0a204` | Head of `IOSkywalkNetworkInterface::registerNetworkInterface`; requires `[RegInfo+4] ≥ 0x108` and version 1 (`0xffffff8002b0a23c`-`0x250`), allocates its own block, attaches it to `[[this+0xC0]]` (`0xffffff8002b0a28b`-`0x298`) and does a `memmove` of 0x108 bytes that includes `+0x3C` (`0xffffff8002b0a29e`-`0x2a9`). |

Sweep of displacements 0x3C, 0xFC and 0xC0 over all the code of IOSkywalkFamily, IO80211Family
and AppleBCMWLANCoreMac (19,190 functions, 1,053,713 instructions): a single reader with the pattern
`[this+0xC0]` + `[copy+0x3C]`.

| VA | Fact |
|---|---|
| `0xffffff8002b0baba` | Head of `IOSkywalkNetworkInterface::getTxDataOffset` (virtual slot 0x9C0 of `IOSkywalkNetworkInterface`'s vtable, `0xffffff8002b3dc88`). It loads `rax = [this+0xC0]` (the copy), returns 0 if it is null or if `[copy]` (version) is 0, and reads `movzx eax, word [rax+0x3C]` at `0xffffff8002b0bad2`. |
| `0xffffff8002affa8e` | Inside `IOSkywalkNetworkBSDClient::start` (head `0xffffff8002aff562`): it calls slot 0x9C0 on the interface and saves the u16 result at `[BSDClient+0x13Ch]` (`0xffffff8002affa94`); the headroom reaches the BSD client this way. |
| `0xffffff80015d92b4`, `0xffffff80015d9583` | Inside `AppleBCMWLANSkywalkInterface::start` (head `0xffffff80015d909a`): two reads of `word [r15+3Ch]` over the driver's original `RegistrationInfo`. `?` The binding of `r15` to the second argument was not fixed instruction by instruction. |

Conclusion: the consumer exists. `getTxDataOffset` is the only read of `+0x3C` on the copy of
`+0xC0`; with defect 6 uncorrected (`+0x3A`) it returns 0 and the BSD client's headroom stays at 0,
so the offset correction is not cosmetic. On the copy of `+0x118` no reader of
`+0x3C` was found. Sites calling through slot 0x9C0 with verified receiver: `IOSkywalkNetworkBSDClient::start`
(`0xffffff8002affa8e`) and `initRegistrationInfo` (`0xffffff8002b10973`, which seeds `RegInfo+0x28` with
the value or 0x20 by default). `?` Which field of the `ifnet` consumes `BSDClient+0x13C` was not followed.

## 2. `IO80211VirtualInterface::free` with a half-initialized object

Head of `init`: `0xffffff80023305f8`. Failure branches (all return 0 in `r14d`):

| Branch | Exit VA | Object state at that point |
|---|---|---|
| 1: `IO80211SkywalkInterface::init` fails | `0xffffff8002330623` | `[this+0x128]` (ivar block) still unwritten: stays 0 if the object allocation is zeroed (see open items). |
| 2: ivars' `IOMallocType` fails | `0xffffff800233063f` | `[this+0x128] = 0` written at `0xffffff8002330635`. |
| 3: `IOLockAlloc` fails | `0xffffff8002330663` | ivars allocated; `ivars+0x30 = 0`. |
| 4: second `IOMallocType` fails | `0xffffff8002330683` | `ivars+0x28 = 0`; the lock of `ivars+0x30` stays allocated. |
| 5: `IO80211FlowQueueDatabase::initialize` fails | `0xffffff8002330746` | `ivars+0xE0` (dictionary) allocated; `ivars+0x200 = 0`; `registerInterestedDriver` already emitted at `0xffffff8002330696`. |

Head of `free`: `0xffffff8002330b8c` (confirmed as slot 18 of `IO80211AWDLProtocol`'s
vtable, `0xffffff8002456440`, and recalculated from five vtables of the sweep).

| Freed ivar | Guard VA | Check |
|---|---|---|
| `[this+0x128]` (ivar block) | `0xffffff8002330b9c`-`0xb9f` | `test`/`jz`: if null it skips the whole block and goes to the base `free`. |
| `ivars+0xE0` (dictionary) | `0xffffff8002330bac`-`0xbaf` | `test`/`jz` before the release (`[vtable+0x28]`). |
| `ivars+0x200` (flow queue base) | `0xffffff8002330bd3`-`0xbd6` | `test`/`jz` before the release. |
| `ivars+0x208` | `0xffffff8002330bf3`-`0xbfd` | `test`/`jz` before the `[vtable+0x8F0]` call. |
| Whole block | `0xffffff8002330c1e` | `memset_s(ivars, 0x210, 0, 0x210)` before `IOFreeTypeImpl`. |

No free of `free` lacks a null check. The base `free`,
`IO80211SkywalkInterface::free` (head `0xffffff80022f6184`, tail of the chain), uses the same
`test`/`jz` pattern on the reviewed frees (`0xffffff80022f61d6`, `0xffffff80022f61fd`,
`0xffffff80022f6224`, ...). The reference caller frees on failure:
`AppleBCMWLANProximityInterface::withOptions` (head `0xffffff80017159e6`) calls the init slot
(`+0x11C8`, occupied by `AppleBCMWLANProximityInterface::init`, `0xffffff8001718db0`) and, if it returns
0, releases through `[vtable+0x28]` (`0xffffff8001715a49`-`0x1715a59`), so `free` runs over
the half-initialized object.

Conclusion: `free` tolerates the half-initialized object on all five branches: every free is
guarded and the fields untouched by `init` are only safe if the object's memory is zeroed.
Residues without crash: the lock of `ivars+0x30` and the block of `ivars+0x28` are not freed in `free`
(`?` alternative owner not identified) and branch 5 leaves the interested driver registration
undone (`?` no cleaner located in the `free` chain).

## 3. `deregisterEthernetInterface`: argument and necessity

Head: `0xffffff8002b10af4`. It forwards `esi` untouched to
`IOSkywalkNetworkInterface::deregisterNetworkInterface(uint)` (`0xffffff8002b10b01`, head
`0xffffff8002b09e9c`) and, if the result is 0, frees the block hanging from `[this+0x118]`
(`0xffffff8002b10b0d`-`0x10b35`). The `uint` argument is not read in either function; the
base implementation does not use it either and always returns 0 (`0xffffff8002b09ff1`).

| Caller | Call VA | Passed value |
|---|---|---|
| `IOSkywalkEthernetInterface::registerEthernetInterface` (packet-queue variant, head `0xffffff8002b10a84`) | `0xffffff8002b10ab5` | 0 (`xor esi,esi` at `0xffffff8002b10ab3`), failure path of `copyRegistrationInfo`. |
| `IOSkywalkEthernetInterface::registerEthernetInterface` (logical-link variant, head `0xffffff8002b10b46`) | `0xffffff8002b10b71` | 0 (`xor esi,esi` at `0xffffff8002b10b6f`), same path. |
| `AppleEthernetRL::stopInterface` (head `0xffffff800295bf2a`) | `0xffffff800295c05f` | 0 (`xor esi,esi` at `0xffffff800295c053`, tail with `jmp`). |

`AppleBCMWLANSkywalkInterface` does not appear among the callers. Broadcom's reference
teardown, `AppleBCMWLANCore::destroySkywalkInterface` (head `0xffffff80016aae4e`), calls slot
`+0x600` with `esi=2` (`0xffffff80016aaebc`) and then slot 18, `free`
(`0xffffff80016aaed7`), without passing through `deregister`. The cleanup does not depend on the explicit call:
`IOSkywalkNetworkInterface::free` (head `0xffffff8002b09ffe`) calls `deregisterNetworkInterface`
unconditionally at the start (`0xffffff8002b0a007`), and that function frees the copy of `+0xC0`, the
`OSOrderedSet` of `+0xC8` and the objects of `+0x98`/`+0xA0`/`+0xD0`.

Conclusion: a driver failing after `registerService` does not need to call
`deregisterEthernetInterface` for cleanup: the object's `free` covers it. The explicit call is
`AppleEthernetRL`'s pattern in `stop`, useful for unplugging the interface while the object lives; the
argument is operationally 0. `?` The `uint` parameter's declared intent is not deducible from the
binary.

## 4. `checkForWork` of the TX completion queue with a null action

Creation: `IOSkywalkTxCompletionQueue::initWithPool` (head `0xffffff8002b129cc`) only requires a non-null pool,
capacity ≥ 2 and last argument 0 (`0xffffff8002b129df`-`0x129f6`); the action is saved without
checking by `IOSkywalkPacketQueue::initWithPool` (head `0xffffff8002b08bc8`, called through
offset 0x240 of ZTV `0xffffff8002b3cc70`), which passes owner and action to the event source's
initialization: the owner lands at `+0x18` and the action at `+0x20`, the offsets later used by
`enqueueAndCompletePackets`. The null action is accepted. The bits activating the path: `+0x28` bit 0
(event source enabled, set by `IOEventSource::enable`, head `0xffffff8000acaa40`, at
`0xffffff8000acaa44`) and `+0xCC` bit 0 (queue initialized, set by `IOSkywalkPacketQueue::initialize`,
head `0xffffff8002b08cce`, at `0xffffff8002b08d85`, and removed by `finalize` at `0xffffff8002b08e23`).

| VA | Fact |
|---|---|
| `0xffffff8002b12d00` | Head of `IOSkywalkTxCompletionQueue::initialize`: writes `[this+0x11C]` and `[this+0x118]` with the same value (`0xffffff8002b12d48`, `0xffffff8002b12d4f`). |
| `0xffffff8002b2db28` | Head of `IOSkywalkTxCompletionQueue::requestEnqueue`. With bit 0 of flags (`dl`): `[this+0x118]++` (`0xffffff8002b2db8e`), `signalWorkAvailable` (`0xffffff8002b2db97`) and returns: it is the asynchronous path leaving pending items. Without the bit, it calls `enqueueAndCompletePackets` under the gate (`0xffffff8002b2db71`). |
| `0xffffff8002b2da78` | Head of `checkForWork` (slot 36, offset 0x120, of the queue's vtable `0xffffff8002b40700`, the same slot 36 as `IOEventSource` at `0xffffff80002a5d70`). If `[0x11C] == [0x118]` it exits (`0xffffff8002b2da84`); if not, it synchronizes, requires both bits and calls `enqueueAndCompletePackets` with `[this+0x58]` (`0xffffff8002b2daa9`). The dispatcher is `IOWorkLoop::runEventSources` (head `0xffffff8000ac9c40`), calling through slot 0x120. |
| `0xffffff8002b2dab2` | Head of `enqueueAndCompletePackets`: after the reentry bit `+0x110`, it calls the action `call [this+0x20]` without checking null (`0xffffff8002b2dae7`) with `(owner, queue, ring, capacity-1, refCon)` and completes whatever it returns with `completePackets` (`0xffffff8002b2db0c`, head `0xffffff8002b12b9e`). |
| `0xffffff8002b2dbb6` | Head of `enqueuePackets`: copies the batch to the ring and calls `completePackets` inline (`0xffffff8002b2dc41`); it touches neither `+0x118` nor the action. |

Conclusion: yes, `checkForWork` can be reached with pending items: an asynchronous
`requestEnqueue` increments `[0x118]` and signals the work loop, and the next pass compares, synchronizes and calls the action without
a guard; with a null action that is a call to address 0 and a panic. The synchronous path of
`requestEnqueue` runs the same risk. In stage 1 wiring the only API used on the TX
completion queue is `enqueuePackets` (via `rtl_txcomp_enqueue` from the TX submission's no-op action),
which never touches `+0x118` nor the action: with that single path the null action is unreachable and `checkForWork`
always exits through the initial comparison. `?` No caller of `requestEnqueue`
over `awdl0`'s queues nor the family point enabling the event source (`+0x28`) on the
datapath's boot has been located; if one existed, the null action would go from latent to fatal. The fix of
defect 1 (non-null action on the TX submission) remains blocking due to the send queue's
`initWithPool`, not because of this route.

## Points left open

| # | Point | Where |
|---|---|---|
| 1 | The initial zero of the object memory and ivars rests on the typed allocator (`OSObject_typed_operator_new` `0xffffff8000a1cc40` → `kalloc_type_impl_external` `0xffffff8000369980` with mask 0xD1004 → heap allocator `0xffffff80003d4230`); the zero fill inside the zone allocator was not followed instruction by instruction. | §2 |
| 2 | Cleanup of the interested driver registration (`registerInterestedDriver`, `0xffffff8002330696`) when branch 5 of `init` fails. | §2 |
| 3 | Owner of freeing the lock `ivars+0x30` and the block `ivars+0x28` of `IO80211VirtualInterface`. | §2 |
| 4 | `ifnet` field consuming `BSDClient+0x13C` after `getTxDataOffset`. | §1 |
| 5 | Binding of `r15` to the `RegistrationInfo` in `AppleBCMWLANSkywalkInterface::start`'s reads. | §1 |
| 6 | Caller of `requestEnqueue` over `awdl0`'s queues and enabling point of the event source (`+0x28`) in the family's datapath boot. | §4 |
| 7 | Declared intent of the `uint` parameter of `deregisterEthernetInterface`/`deregisterNetworkInterface` (operationally ignored; the three callers pass 0). | §3 |
