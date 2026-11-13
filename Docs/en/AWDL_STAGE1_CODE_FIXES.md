# Proposed fixes for the stage 1 code

Patch proposal, not applied, for the six defects of the table §4 of
`AWDL_STAGE1_SPEC_DELTA.md`, cross-checked against the later documents and the disassembly.
Base: kernelcache of macOS 26.7.1 (25G241, x86_64); IDA headless (`idat -A -S`) over copies of
`boot_awdl.kc.i64`. No code, patch or specification file has been edited.

The lines are those of the tree at the time of writing. The block of 50 overrides of
`AWDLInterface.h` is at `:91-145`; the delta cites it as `:96-145` (five lines of shift).
The numbers of `AWDLInterface.cpp` and of `Tools/awdl_stage1_hook.patch` do match the delta.

## 0. Starting verification

| Check | Result |
|---|---|
| `clang++` of `AWDLInterface.cpp` with the flags of `build_airlayer.sh` | compiles, `rc=0`, 62 792 B (`/tmp/awdlobj/AWDLInterface.o`); the kext has not been linked |
| `nm -u` of the object | 540 undefined symbols; **93** of class `IO80211VirtualInterface` |
| The previous 93 in `Docs/re/io80211_25.6.0/syms.txt` | **93/93 with type `0f`**; none unresolved |
| The 50 overrides of `:91-145` | 48 appear as undefined (all `0f`); `free` and `stop` have local bodies and do not appear |
| Undefined `IO80211*`/`IOSkywalk*` symbols without `0f` in the two dumps | **0** |
| Function head of the 50 cited addresses and the 9 auxiliary ones | 59/59 `get_func(ea).start_ea == ea` |

The addresses of the §5 table are nlist symbol addresses; it was verified that each one is a
function head with `ida_funcs.get_func`. No interior addresses are cited.

Summary of the six defects and their order:

| # | Defect | File:line | Severity | Blocking |
|---:|---|---|---|---|
| 1 | Null action in the TX submission queue | `AWDLInterface.cpp:140` | boot fails, no `awdl0` | **yes** |
| 2 | Rollback without removing the queues from the work loop | `AWDLInterface.cpp:186-205` | dangling event if registration or name fails | no |
| 3 | `rtl_vif_init` declared `void`, result ignored | `AWDLInterface.cpp:47`, `:110` | loses the base `init` failure | no |
| 4 | Publication with `setAwdlInterface`, no reader | `AWDLInterface.cpp:270-273`, `.h:407` | useless surface | no |
| 5 | 50 overrides without checking the link | `AWDLInterface.h:91-145` | none: 93/93 resolve `0f` | no |
| 6 | Headroom at `+0x3A` instead of `+0x3C` | `AWDLInterface.cpp:82` | corrupts the high half of `unit` and leaves headroom 0 | no |

## 1. Defect 1: TX submission queue action

### 1.1 Evidence

| VA | Fact |
|---|---|
| `0xffffff8002b0c9b4` | head of `IOSkywalkTxSubmissionQueue::withPool`; creates the object and calls the virtual `initWithPool`; if it returns 0, releases it and returns null. |
| `0xffffff8002b0caac` | head of `IOSkywalkTxSubmissionQueue::initWithPool`; stores `if (a4 >= 8 && a2 != nullptr && a8 != nullptr && a10 <= 0x1F)`. `a8` is the packet action, with the signature `unsigned int (*)(OSObject*, IOSkywalkTxSubmissionQueue*, IOSkywalkPacket* const*, unsigned int, void*)`. With a null action it returns 0 and `withPool` returns null. |
| `0xffffff8002b2c860` | head of `IOSkywalkTxSubmissionQueue::legacyDequeue`; calls the action with `(owner, this, ring+index, pending, refCon)` and uses the return as the **number of consumed packets**: with 0 it sets the stop bit (`|2`), stores the instant and exits without advancing the ring; with > 0 it advances the ring by that amount; if it is less than the batch, it counts a partial drain. |
| `0xffffff8002b2dbb6` | head of `IOSkywalkTxCompletionQueue::enqueuePackets`; with the queue enabled it copies the batch and calls `completePackets` under its gate, which is who completes and returns the packets to the pool. It does not use the `IOEventSource` action. |
| `0xffffff8002b2da78` | head of `IOSkywalkTxCompletionQueue::checkForWork`; only acts if there are pending ones and the queue is enabled; it is not the path `enqueuePackets` uses. `?` Whether that path can be reached with a null action and pending packets; with the no-op below the batch completes inside `enqueuePackets`, so nothing should remain pending. |

Verified in the disassembly: `legacyDequeue` stores the action return as `unsigned int`; with 0 it
sets bit `|2` and exits, with `n` it advances the ring `n` positions and adds `n` to the counter.
`enqueuePackets` (`0xffffff8002b2dbb6`) requires `0 < count < *(this+0xAC)` (queue capacity, 128 at
`AWDLInterface.cpp:141`, so the batches of 64 comply) and returns the `0xE00002C2`-family in the
other cases; the second `unsigned int` of the mangling is unused. `deallocatePacket` resolves to
`0xffffff8002b2c0de`.

Conclusion: the action must **consume the whole batch and return `count`**; returning 0 stalls the
queue
and the packets stay in the ring. It must not merely return the number without releasing the
packets:
`legacyDequeue` does not free them, and the ring stops referencing them when it advances, so the
pool loses them. The path with the property already proven on the STA path is returning them via
the TX completion queue
(`RTLAirLayer.cpp:418-421`); if that send fails, they are freed to the pool so as not to leak
memory.

The signature is copied from `RTLAirInterface::txAction` (`RTLAirLayer.hpp:118-119`), which is the
one already accepting
`withPool` in production: the second parameter is the queue and the third the packet vector; the
`extern` of `rtl_txsub_withPool` passes them as `void *`, so type checking does not intervene.

### 1.2 Before and after

`Source/AirLayer/AWDLInterface.h:396-397` (before):

```cpp
	bool createDatapath();
	static IOReturn removeQueues(OSObject *owner, void *, void *, void *, void *);
```

After:

```cpp
	bool createDatapath();
	void teardownDatapath();
	static IOReturn txNoop(OSObject *owner, void *queue, void *const *pkts,
	                       uint32_t count, void *refCon);
	static IOReturn removeQueues(OSObject *owner, void *, void *, void *, void *);
```

`Source/AirLayer/AWDLInterface.cpp`, after the `extern` of `rtl_rxcomp_withPool` (`:44-46`), add:

```cpp
IOReturn rtl_txcomp_enqueue(void *q, void *const *pkts, uint32_t n, uint32_t opts)
	__asm("__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj");
void rtl_pool_free(void *pool, void *pkt)
	__asm("__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket");
```

`Source/AirLayer/AWDLInterface.cpp:140` (before):

```cpp
	_txSubQ = rtl_txsub_withPool(_txPool, 128, 0, this, nullptr, nullptr, 0);
```

After:

```cpp
	_txSubQ = rtl_txsub_withPool(_txPool, 128, 0, this,
	                             (void *)&RTLAwdInterface::txNoop, nullptr, 0);
```

Implementation, after `removeQueues` (`:228`):

```cpp
/*
 * TX submission action, stage 1: nothing is transmitted. The packets go back
 * through the TX completion queue, as RTLAirInterface::txAction does once the
 * radio has consumed them; the completion queue completes them under its gate
 * and returns them to the pool. legacyDequeue advances its ring by the returned
 * count, so returning 0 would stall the queue: consume the whole batch.
 */
IOReturn RTLAwdInterface::txNoop(OSObject *owner, void *, void *const *pkts,
                                 uint32_t count, void *)
{
	RTLAwdInterface *me = OSDynamicCast(RTLAwdInterface, owner);
	if (!me || !count || !me->_txCompQ)
		return 0;
	/* enqueuePackets requires count < capacity; 64 < 128 always. */
	for (uint32_t done = 0; done < count; done += 64) {
		uint32_t n = count - done > 64 ? 64 : count - done;
		if (rtl_txcomp_enqueue(me->_txCompQ, pkts + done, n, 0) != kIOReturnSuccess)
			for (uint32_t i = 0; i < n; i++)
				rtl_pool_free(me->_txPool, pkts[done + i]);
	}
	return (IOReturn)count;
}
```

The `(void *)&RTLAwdInterface::txNoop` replicates the cast of `RTLAirLayer.cpp:365`; the `extern`
declares the
action as `void *`, so the compiler does not check the signature and only the ABI counts.

`?` The path of `checkForWork` (`0xffffff8002b2da78`) with a null action and pending packets has
not been provoked: there is no known route that leaves pendings if `enqueuePackets` completes them
immediately.

## 2. Defect 2: datapath rollback

### 2.1 Evidence

`RTLAwdInterface::removeQueues` (`:212-228`) already does what is missing: disables, removes from
the work loop,
frees and nulls each queue, skipping the already-null ones. `stop` (`:230-238`) already chooses
between
`runAction` and a direct call depending on whether there is a work queue. The failure is in
`startWithRegistration`: the two
`return false` after `createDatapath` (`:192-193` and `:201-203`) leave the three queues on the
work loop; the patch does `terminate()` + `release()` (`Tools/awdl_stage1_hook.patch:175-179`) and
`free()`
(`:240-252`) frees the pointers without removing them first, leaving the work loop with dangling
events.

### 2.2 Before and after

`Source/AirLayer/AWDLInterface.cpp:190-193` (before):

```cpp
	IOReturn r = rtl_eth_register(this, _regInfo, queues, 3, _txPool, _rxPool, 0);
	LOG("awdl: registerEthernetInterface 0x%x", r);
	if (r != kIOReturnSuccess)
		return false;
```

After:

```cpp
	IOReturn r = rtl_eth_register(this, _regInfo, queues, 3, _txPool, _rxPool, 0);
	LOG("awdl: registerEthernetInterface 0x%x", r);
	if (r != kIOReturnSuccess) {
		teardownDatapath();
		return false;
	}
```

`Source/AirLayer/AWDLInterface.cpp:201-203` (before):

```cpp
	if (!name || !name[0]) {
		LOG("awdl: no bsd name");
		return false;
	}
```

After:

```cpp
	if (!name || !name[0]) {
		LOG("awdl: no bsd name");
		teardownDatapath();
		return false;
	}
```

Helper and `stop` (`:230-238`):

```cpp
void RTLAwdInterface::teardownDatapath()
{
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
	if (wq)
		wq->runAction(&RTLAwdInterface::removeQueues, this);
	else
		removeQueues(this, nullptr, nullptr, nullptr, nullptr);
}

void RTLAwdInterface::stop(IOService *provider)
{
	teardownDatapath();
	IO80211VirtualInterface::stop(provider);
}
```

`removeQueues` is idempotent: the pointers are nulled and later calls do nothing, so
a `terminate()` reaching `stop()` after the rollback does not repeat work. If the failure is
of `createDatapath` before `addEventSource` (`:143-150`), the queues are not on the work loop and
`free()` frees them; no teardown is needed.

`?` The failed registration with an empty name leaves the `registerService` done and it is not
deregistered: the
specification §11 asks for `deregisterEthernetInterface(uint)` (symbol `0f` at
`0xffffff8002b10af4`), but
the value of the argument (BSD unit) is not fixed. It stays out of this proposal.

## 3. Defect 3: result of `rtl_vif_init`

### 3.1 Evidence

`IO80211VirtualInterface::init(IO80211Controller *, ether_addr *, uint32_t, const char *)` is at
`0xffffff80023305f8`. It returns 0 on every failure branch (`LODWORD(v6) = 0`: super `init`,
`IOMalloc`,
`IOLockAlloc`, `bpfLock`, `bpfListHead`, denied processes array) and 1 on the success one
(`LOBYTE(v6) = 1`). The header itself declares it `bool` at `AWDLInterface.h:149`. The `extern` of
`AWDLInterface.cpp:47-49` declares it `void`, so the value is discarded and `initWithController`
returns `true` with the base `init` failed.

### 3.2 Before and after

`Source/AirLayer/AWDLInterface.cpp:47-49` (before):

```cpp
void rtl_vif_init(void *self, IO80211Controller *ctrl, ether_addr *mac, uint32_t role,
                 const char *name)
	__asm("__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc");
```

After:

```cpp
bool rtl_vif_init(void *self, IO80211Controller *ctrl, ether_addr *mac, uint32_t role,
                  const char *name)
	__asm("__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc");
```

`Source/AirLayer/AWDLInterface.cpp:110` (before):

```cpp
	rtl_vif_init(this, ctrl, &ea, AWDL_ROLE, "awdl");
```

After:

```cpp
	if (!rtl_vif_init(this, ctrl, &ea, AWDL_ROLE, "awdl")) {
		LOG("awdl: base init failed");
		return false;
	}
```

The return type change does not alter the symbol (the return is not part of the mangling) and the
convention is the one the header already uses. The failure reaches the patch, which does
`terminate()` +
`release()` (`Tools/awdl_stage1_hook.patch:175-179`).

`?` `free()` over a half-done base `init` calls `IO80211VirtualInterface::free`; it is the usual
IOKit pattern (each ivar is freed if it exists), but `free` has not been decompiled to confirm it
tolerates every partial state of `init`.

## 4. Defect 4: publication with `setAwdlInterface`

### 4.1 Evidence

`IO80211Controller::setAwdlInterface` (`0xffffff8002299c24`) and `getAwdlInterface`
(`0xffffff8002293256`) have **0 xrefs** in the whole kernelcache. The 97 does not consult them: it
reads
`getPrimarySkywalkInterface` (`AWDL_STAGE1_VIRTUAL_IF_PARENT.md` §3-§7), and the primary is
already attached
(`RTLAirLayer.cpp:1493`). The `_awdl` object of the controller remains necessary for the EEXIST and
the teardown; what is superfluous is the family call.

### 4.2 Before and after

`Source/AirLayer/AWDLInterface.cpp:52-53` (delete):

```cpp
IOReturn rtl_ctrl_setAwdlInterface(IO80211Controller *ctrl, void *awdl)
	__asm("__ZN17IO80211Controller16setAwdlInterfaceEP23IO80211VirtualInterface");
```

`Source/AirLayer/AWDLInterface.cpp:268-273` (delete the whole block):

```cpp
/* Publishes the interface on the controller, so getAwdlInterface() answers it.
 * The handler calls it once the start succeeded; NULL unpublishes. */
IOReturn rtlAwdlPublish(IO80211Controller *ctrl, RTLAwdInterface *awdl)
{
	return rtl_ctrl_setAwdlInterface(ctrl, awdl);
}
```

`Source/AirLayer/AWDLInterface.h:405-407` (delete):

```cpp
/* IO80211Controller::setAwdlInterface, wrapped so the handler that creates the
 * interface does not carry the family symbol. NULL unpublishes it. */
IOReturn rtlAwdlPublish(IO80211Controller *ctrl, RTLAwdInterface *awdl);
```

`Tools/awdl_stage1_hook.patch:183` (delete `rtlAwdlPublish(this, awdl);`) and `:200` (delete
`rtlAwdlPublish(this, nullptr);`). The rest of `createAwdlInterface` and `destroyAwdlInterface`
does not change: `_awdl` keeps holding the object and `terminate()` + `release()` remain the
teardown.

The header comment of `AWDLInterface.h:1-22` («the interface exists and is
published») should be adjusted so as not to describe a publication that is no longer done.

`?` Whether a later stage needs `getAwdlInterface` for another client; today there is no
reference in the kernelcache.

## 5. Defect 5: the 50 overrides of `AWDLInterface.h:91-145`

No change required. `AWDLInterface.cpp` was compiled and every undefined symbol cross-checked
against
`Docs/re/io80211_25.6.0/syms.txt`; the 93 symbols of `IO80211VirtualInterface` the object emits
have type `0f` and **none** would stay unresolved. Of the 50 declared, 48 reach the object (the
other two are `free` and `stop`, which `RTLAwdInterface` defines) and the 48 are `0f`.

| Slot | Method | Exported symbol |
|---|---|---|
| `0x428` | `configureReport` | `0f ffffff80023343f2` |
| `0x430` | `updateReport` | `0f ffffff80023344e0` |
| `0x5C0` | `start` | `0f ffffff80023308aa` |
| `0x600` | `terminate` | `0f ffffff8002330c50` |
| `0x670` | `attach` | `0f ffffff800233023a` |
| `0x678` | `detach` | `0f ffffff800233027c` |
| `0x770` | `newUserClient` | `0f ffffff80023349d8` |
| `0x780` | `stringFromReturn` | `0f ffffff8002330b24` |
| `0x8E8` | `prepareBSDInterface` | `0f ffffff8002331ada` |
| `0x910` | `processBSDCommand` | `0f ffffff8002331b36` |
| `0x930` | `setInterfaceEnable` | `0f ffffff8002331ba6` |
| `0x9B8` | `getFeatureFlags` | `0f ffffff8002331b4a` |
| `0x9E0` | `bpfTap` | `0f ffffff8002330e42` |
| `0xA88` | `setMulticastAddresses` | `0f ffffff8002331c6c` |
| `0xB08` | `createPeerManager` | `0f ffffff80023308c2` |
| `0xB18` | `postMessage` | `0f ffffff8002333192` |
| `0xB38` | `logTxPacket` | `0f ffffff8002333fae` |
| `0xB40` | `logTxCompletionPacket` | `0f ffffff800233403a` |
| `0xB50` | `inputPacket` | `0f ffffff8002333cac` |
| `0xB60` | `logSkywalkTxReqPacket` | `0f ffffff800233218a` |
| `0xB68` | `pendingPackets` | `0f ffffff8002332378` |
| `0xB70` | `packetSpace` | `0f ffffff8002332446` |
| `0xB88` | `linkState` | `0f ffffff80023333f8` |
| `0xB90` | `setScanningState` | `0f ffffff8002331eb0` |
| `0xBB0` | `updateLinkParameters` | `0f ffffff8002331f70` |
| `0xBB8` | `updateInterfaceCoexRiskPct` | `0f ffffff8002332010` |
| `0xBE8` | `setInterfaceNF` | `0f ffffff8002333862` |
| `0xC00` | `setDebugFlags` | `0f ffffff8002333376` |
| `0xC08` | `debugFlags` | `0f ffffff80023333e2` |
| `0xC10` | `setInterfaceChipCounters` | `0f ffffff8002333884` |
| `0xC18` | `setInterfaceMIBdot11` | `0f ffffff80023338a2` |
| `0xC20` | `setFrameStats` | `0f ffffff80023338c0` |
| `0xC28` | `setInfraSpecificFrameStats` | `0f ffffff80023338de` |
| `0xC38` | `getWmeTxCounters` | `0f ffffff800233209e` |
| `0xC48` | `setWoWEnabled` | `0f ffffff80023333f2` |
| `0xC50` | `wowEnabled` | `0f ffffff8002334c34` |
| `0xC58` | `printDataPath` | `0f ffffff8002334344` |
| `0xC68` | `findOrCreateFlowQueue` | `0f ffffff800233270a` |
| `0xC70` | `findOrCreateFlowQueueWithCache` | `0f ffffff8002332c38` |
| `0xC78` | `findExistingFlowQueue` | `0f ffffff80023324e4` |
| `0xC80` | `removePacketQueue` | `0f ffffff80023322e6` |
| `0xC88` | `flushPacketQueues` | `0f ffffff8002332290` |
| `0xC90` | `cachePeer` | `0f ffffff80023324fc` |
| `0xC98` | `shouldLog` | `0f ffffff80023343d6` |
| `0xCA0` | `vlogDebug` | `0f ffffff800233436a` |
| `0xCA8` | `vlogDebugBPF` | `0f ffffff80023343d0` |
| `0xCC0` | `getP2PSkywalkPeerMgr` | `0f ffffff8002334c1a` |
| `0xCD0` | `findPeer` | `0f ffffff8002334ec6` |
| `0xDB8` | `getEventPipeSize` | `0f ffffff80023347d2` |
| `0xDC0` | `createEventPipe` | `0f ffffff80023347fc` |

None should be removed: they are overrides of already existing slots and their only effect is that
the vtable cell points to the family implementation instead of to that of
`IO80211SkywalkInterface` or
`IOService`. The link check P0.4 asks for can be repeated without IDA:

```sh
clang++ $CXXFLAGS -c Source/AirLayer/AWDLInterface.cpp -o /tmp/AWDLInterface.o
nm -u /tmp/AWDLInterface.o | grep '__ZN23IO80211VirtualInterface' |
while read s; do grep -q " 0f $s\$" Docs/re/io80211_25.6.0/syms.txt || echo "SIN 0f: $s"; done
```

`?` The `nm` listing must be done with the freshly compiled object: if an override is removed from
the header, the compiler resolves that cell again in the base class and the failure appears as a
symbol of another class, not this one.

## 6. Defect 6: headroom at `+0x3A` versus `+0x3C`

### 6.1 Evidence

`AppleBCMWLANCore::createProxInterface` (head `0xffffff800165dcbe`) builds the `RegistrationInfo`
of role 6 before calling `AppleBCMWLANProximityInterface::start`. The table addresses are
instructions inside that function, not heads:

| Instruction | Write |
|---|---|
| `0xffffff800165de82` | `[r12+30h] = r15` (prefix pointer) |
| `0xffffff800165de87` | `[r12+38h] = r14d` (BSD unit) |
| `0xffffff800165de8c` | `word [r12+3Ch] = 40h` (**64: headroom**) |
| `0xffffff800165de94` | `[r12+40h] = eax` (flags) |

`IOSkywalkEthernetInterface::initRegistrationInfo` (head `0xffffff8002b108e2`) writes
`*(_DWORD *)(a2 + 56) = getBSDUnitNumber()` (`+0x38`) and **does not** touch `+0x3C` (the 0x128-byte
`bzero` leaves it at 0). `IOSkywalkNetworkInterface::registerNetworkInterface` (head
`0xffffff8002b0a204`) copies
`a2[9]` and `a2[10]` (`+0x24` and `+0x28`) to the descriptor (`+0x30`/`+0x34`), and composes the
name with the
interface getters (`vtable+2536`/`+2544`), not with `+0x30`/`+0x38`; the 0x108-byte
`memmove`
includes `+0x3C`, so the headroom travels in the copy.

The current code writes `u16 64` at `+0x3A`:

- `+0x3A` is the high half of the unit `u32` at `+0x38`; with `unit = 0` at `:179`, the value stays
  `0x00400000` (4 194 304) and the real headroom (`+0x3C`) remains 0.
- The name of `awdl0` does not change because of this field: the properties
  `IOInterfaceNamePrefix` /
  `IOInterfaceUnit` fix it, and they come from the interface getters.
- The copy at `interface+0xC0` is left with a corrupted unit and headroom 0.

### 6.2 Before and after

`Source/AirLayer/AWDLInterface.cpp:82` (before):

```cpp
#define AWDL_REGINFO_HEADROOM   0x3a    /* u16, 64                           */
```

After:

```cpp
#define AWDL_REGINFO_HEADROOM   0x3c    /* u16, 64; +0x38 is the BSD unit     */
```

`:180` does not change:

```cpp
	*(uint16_t *)(_regInfo + AWDL_REGINFO_HEADROOM) = 64;
```

`?` No reader of `+0x3C` has been located in the 0x108-byte copy; the field is seeded like
on the reference path, but its consumer (if any) is not identified.

## 7. Order of application and independence

| Order | Defect | Files | Depends on |
|---:|---|---|---|
| 1 | 6, headroom | `AWDLInterface.cpp:82` | nothing |
| 2 | 3, `rtl_vif_init` | `AWDLInterface.cpp:47-49`, `:110` | nothing |
| 3 | 1, no-op action | `AWDLInterface.h:396-397`, `AWDLInterface.cpp:44-46`, `:140`, `:228+` | nothing; **blocking** |
| 4 | 2, rollback | `AWDLInterface.cpp:190-193`, `:201-203`, `:230-238`, `.h:396` | 3 to be testable (without the TX queue there is no datapath to remove), but the lines are different |
| 5 | 4, `setAwdlInterface` | `AWDLInterface.cpp:52-53`, `:268-273`, `.h:405-407`, `awdl_stage1_hook.patch:183`, `:200` | nothing |
| 6 | 5, overrides | none: only the `nm` check | nothing |

Independent of each other: defects 6, 3, 1 and 4. Defects 1 and 2 touch the same function
(`startWithRegistration`), but different lines: apply 1 before 2. Defect 4 is the only
one touching the patch and the header at once; if applied without 1, the patch still compiles (the
symbol stops being used).

After applying, and with express OK to compile:

1. `./build_airlayer.sh` and check it links; if it fails on a
   `__ZN23IO80211VirtualInterface...` symbol, the table of §5 says which one and whether it is
   `0f`.
2. Repeat the `nm -u` of §5 on `Build/obj_airlayer/AWDLInterface.o`.
3. Load with `rtlawdl=1` and run T3.5 (bring up `awdl0` and provoke IPv6/ND): with the no-op the
   packets complete and drop, and there must be no panic; with the rollback, a registration
   failure must not leave dangling events on the work loop.

## 8. Pendings this proposal does not close

- `?` Reader of `+0x3C` in the copy of the `RegistrationInfo` (§6).
- `?` `deregisterEthernetInterface` and its argument on the empty-name failure (§2).
- `?` Tolerance of `IO80211VirtualInterface::free` to a half-done base `init` (§3).
- `?` `checkForWork` path of the TX completion queue with a null action (§1).
- `?` Future need of `getAwdlInterface` (§4).
