# Skywalk datapath of AppleBCMWLANProximityInterface (AWDL role 6)

What a role 6 interface must provide to move packets: Skywalk objects it creates (queues, pools, sizes and options), how TX reaches the chip, how RX is delivered to the stack, and what `getBSDNamePrefix` returns. Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), macOS 25.6.0, Hex-Rays decompilation of two filesets of the same KC: `com.apple.driver.AppleBCMWLANCoreMac` (interface and core) and `com.apple.driver.AppleBCMWLANBusInterfacePCIeMac` (bus and PCIe queues). Continues `Docs/AWDL_ROLE6_RE.md` (life cycle) and `Docs/AWDL_PROXIMITY_VTABLE_HIERARCHY.md` (inheritance and slots); the interface vtable offsets cited here match those of that document. Addresses = KC VAs. Unobserved items go with `?`.

**Short summary:**

1. The interface creates no queues or pools: it asks the bus for them (`AppleBCMWLANPCIeSkywalk::registerSkywalkInterface`, slot +0x118) with a 12-qword descriptor (`skywalkInterfaceRegisterParams_t`) that points to arrays of its ivars, and the bus creates and returns **4 TX submission queues (one per WME AC), 1 TX completion, 1 RX completion and 1 multicast queue**, all over **a single shared pool** `AppleBCMWLANSkywalkPool` (by default 4864 packets × 2048 bytes, `maxBuffersPerPacket=1`, flags=1, no DMA spec in PoolOptions).
2. The queues are Broadcom/PCIe subclasses of the queues of `IOSkywalkFamily` (`AppleBCMWLANPCIeSkywalkTxSubmissionQueue`, `…RxCompletionQueue`, `…MulticastQueue`), not the family classes: they add own virtuals (+0x308–0x368) with which the interface governs them (`triggerAWDLMulticastTx`, `p2pMulticastTx`, `freeMulticastPacket`, `stagePacket`, `freePendingPackets`, `collectQueueStats`, `flushPackets`).
3. TX: the stack enqueues into the TxSubmissionQueue of Skywalk; the `dequeuePackets` action of the PCIe subclass classifies by role, and for role 6 inserts a SNAP header, duplicates the multicast for the second channel (`stashMulticastPacketPair`) and enqueues into an `AppleBCMWLANPCIeFlowQueue` per destination (max. 16 per queue) which kicks its firmware flow ring.
4. RX: `AppleBCMWLANPCIeSkywalk::rxCompRingDrain` drains the completion rings, for role 6 validates length ≥ 28, processes VLAN/SNAP, calls the pre-hook of the interface (slot +0x8F8) and for each packet does `enqueuePackets` into the **RxCompletionQueue of the destination interface** (obtained via slot +0xD70); at the end of the batch it kicks `requestEnqueue(0,0)`/`flushPackets` on the RX queues of the 12 interfaces.
5. `getBSDNamePrefix` returns a pointer to the literal `"awdl"`; that same literal is the name `createProxInterface` passes to `init`, it goes into `RegistrationInfo+0x30` and the final BSD name (`awdl0`) is assembled by `IO80211SkywalkInterface::getBSDName` with `"%s%u"` from the ifnet.

## 1. Method and record cards

Decompiled functions (all OK, no failures): the 15 requested methods of `AppleBCMWLANProximityInterface` plus `withOptions`, `init×2`, `free`, `enable/disable(unsigned)`, `setInterfaceEnable`, `setPromiscuousModeEnable` (which is `validateDispatchQueue` + `return 0`), `isChipInterfaceReady`, `deleteChipInterface`, `setCOMPANION_SKYWALK_IF_CREATE`, `AppleBCMWLANCore::createProxInterface`/`createSkywalkInterface`, the full AppleBCMWLANSkywalkInterface block (infrastructure equivalent), the family block `IOSkywalkPacketBufferPool/Queue/EthernetInterface`, the bases `IO80211VirtualInterface`/`IO80211SkywalkInterface`, and the PCIe block (`registerSkywalkInterface`, `allocSkywalkCommonResources`, pool, TX/RX queues, `rxCompRingDrain`, `dequeuePackets`, `triggerAWDLMulticastTx`, `p2pMulticastTx`). Vtables dumped slot by slot with names: `AppleBCMWLANProximityInterface` (572 slots), `AppleBCMWLANBusSkywalk`/`AppleBCMWLANPCIeSkywalk`, the four PCIe queue classes and the family ones. The slots cited below are all resolved by numeric dump, not by sampling. The read of the base bus vtable in `AWDL_PROXIMITY_VTABLE_HIERARCHY.md` remains valid; here the concrete runtime object is `AppleBCMWLANPCIeSkywalk` (subclass living in the PCIe fileset), whose slot +0x118 points to the real registration implementation.

## 2. Creation: `withOptions` and `init`

`AppleBCMWLANCore::createProxInterface` (`0xffffff800165dcbe`): if the AWDL slot of the core is already occupied, log "already created" and success; otherwise:

1. `AppleBCMWLANProximityInterface::withOptions(core, request+4, "awdl")` (`0xffffff80017159e6`): `OSObject_typed_operator_new` of 320 bytes, ctor of `IO80211AWDLProtocol` with its own metaclass, vptr = `0xffffff80017f5b78`, `instanceConstructed`, and call via slot +0x11C8 to `init(core, mac, 6, "awdl")`. If `init` fails → release and null (the caller returns `0xE00002BC`).
2. `init(core, mac, role, name)` (`0xffffff8001718db0`):
   - own `init()` (slot +0x88): `IO80211SkywalkInterface::init()` (via the imported vtable of the base) + `IOMallocType` of **0x460 bytes of ivars**.
   - `IO80211VirtualInterface::init(ctrl, mac, role, name)` (qualified call via the imported vtable of `IO80211AWDLProtocol`, slot +0xE78; `0xffffff80023305f8`): in turn `IO80211SkywalkInterface::init(ctrl, mac)`, own ivars of the base, `setInterfaceRole(6)`, `IOLockAlloc`, BPF list, `registerInterestedDriver(ctrl)` + `ctrl->open(this)`, dictionary of denied processes (capacity 5), and `IO80211FlowQueueDatabase::initialize()` → base ivars+512.
   - `setInterfaceId(2)` if the core version ≥ 5; `0xFFFFFFFF` otherwise.
   - `AppleBCMWLANTaskmaster::withWorkQueue(core→getWorkQueue, 0x10)` → ivars+56 (init fails without a Taskmaster).
   - State ivars: core→+72, role 6 →+688, `+932 = 0` ("chip interface ready" state, checked by `isChipInterfaceReady` and `enableDatapath`), state machine times (lifetime 6, guard 20, maxNoMaster 4, etc. at +656…+800), boot-args `awdl_log_flags_config`, `awdl_max_peers` (+908, default −1), `awdl_low_latency_default_mapping` (+912, 1), `awdl_low_latency_tx_requeue` (+916, 0).
   - Tx Power Manager of the core →+1000 (init fails without it), `AppleBCMWLANCommander` of the core →+968 (used by `setMacAddress`).
   - **Count and map of TX queues: `ivars+945 (byte) = 4`; AC map `ivars+948 = {0,1,2,3}`** (two qwords `0x100000000`, `0x300000002`). The queues do not exist yet: the bus will create them in `start`.
3. `obj->attach(core)` via virtual slot +0x670 (`IO80211VirtualInterface::attach`, `0xffffff800233023a`).
4. `IOSkywalkEthernetInterface::initRegistrationInfo(reg, 1, 0x130)` over a 0x130-byte `RegistrationInfo` on the caller's stack, and Broadcom fill: `+0x0C = 3` (Wi-Fi subfamily), `+0x14 = 2`, `+0x24 = 0x8000000080` (qword), `+0x30 = &"awdl"` (pointer to the prefix), `+0x38 = 0`, `+0x3A = 64` (u16), `+0x40 = 6` (u32; 4 if the `IOPMResetPowerStateOnWake` property of the bus is not true), `+0x108 = MAC` (6 bytes taken from `getSelfMacAddr`, slot +0xCF8). `initRegistrationInfo` (`0xffffff8002b108e2`) validates exactly version 1 and size 304 (0x130) and preloads defaults: subfamily 2 at +0x08, family at +0x0C, 256 at +0x14, 32 at +0x24, prefix "en" at +0x30, BSD unit at +0x38, 46 at +0x44 and MTU 1500 at +0x48/+0x4C.
5. `AppleBCMWLANProximityInterface::start(obj, core, reg)` (the specialized one) and, if it returns true, the BSD name is copied into the request.

## 3. `start(AppleBCMWLANCore*, RegistrationInfo*)` — registration against the bus

`0xffffff8001729376`. The `start(IOService*)` (slot +0x5C0) only does `safeMetaCast(provider, AppleBCMWLANCore)` and delegates here with `reg = 0`. Steps:

1. **Base:** qualified call `IO80211VirtualInterface::start(provider)` via the vtable of `IO80211AWDLProtocol` (+0x5C0). Fails → log "awdl skywalk start fail" and false. The base (`IO80211SkywalkInterface::start`, `0xffffff80022f5792`) creates the CCPipe/CCStream of the interface (`"Interface_<class>_<n>"` with `n` = counter of instances per role), PM (`PMinit`, `joinPMtree`, 2 states), `IO80211FlowQueueDatabase`, array of pipes, and `IO80211MacAddressAgent`. The peer manager: for role 2 it is inherited from the parent; for the rest (including 6) one of its own is created per virtual. **The WCL glue (`IO80211Glue`) is only created for role 1** — the AWDL interface has no WCL route.
2. Own logger (slot +0xD08) → base ivars+64; `core` → ivars+72.
3. Bus: `core` (slot 397, `getWorkQueue`) → `IO80211WorkQueue` → ivars+1104; bus interface of the core (state +296 → +5424, virtual +0x1F8) and `safeMetaCast` to `AppleBCMWLANBusSkywalk` → ivars+984/+992. At runtime the object is `AppleBCMWLANPCIeSkywalk` (subclass of the PCIe fileset).
4. **Descriptor** `skywalkInterfaceRegisterParams_t` (12 qwords, table in §4) filled with pointers to the ivars of the interface and headroom 64 (constant u16) and logger; call `bus->registerSkywalkInterface(&descriptor)` (slot +0x118). Returns non-zero → log "Failed to register interface with bus interface" and false.
5. The bus (§5) creates the queues and leaves them **inside the ivars arrays** of the interface. Back in `start`: for each TX submission queue (count in `ivars+945`) and for TX completion and RX completion: `IO80211WorkQueue::addEventSource(queue)` (slot +0x140 of the work queue) and `IOSkywalkPacketQueue::getQueueId` → `ivars+1088[map[i]]` (map at `ivars+948`); if `ivars+1064` (multicast queue) is non-null it is added too. For the infra route the order is the same (mirror pseudocode in `AppleBCMWLANSkywalkInterface::start`, `0xffffff80015d909a`).
6. `RegistrationInfo+0x48 and +0x4C = 0x594` (two u32, 1428): fixed post-headroom MTU of AWDL (the infra route instead subtracts the headroom of the `RegistrationInfo` from the values `initRegistrationInfo` left).
7. `IOSkywalkEthernetInterface::registerEthernetInterface(reg, queues, n+2, txPool, rxPool)` — **the direct registration against IOSkywalkFamily** (the infra route uses the wrapper `IO80211InfraInterface::registerInfraEthernetInterface`; decompiled: it does `copyRegistrationInfo` and calls `IOSkywalkNetworkInterface::registerNetworkInterface(this, logicalLink, queues, count, txPool, rxPool, 0)`). In the Proxi pseudocode the decompiler does not show the argument registers; the shape `reg + copied queues (txsub…, txcomp, rxcomp) + n+2 + pools of ivars +1072/+1080` is deduced from the infra mirror and from the signature of the callee (`?` in the fine reconstruction). Error → log with the code and rollback (removeEventSource of all the queues).
8. Success: `IOService::registerService(2)` (slot +0x5B0; observed options value 2, `?` its exact semantics) and `IO80211SkywalkInterface::setRunningState(0)` (slot +0x938).

## 4. Descriptor `skywalkInterfaceRegisterParams_t`

Observed fields (descriptor offsets / Proxi ivar / infra ivar; `in` = the interface fills it, `out` = the bus writes it):

| qword | content | Proxi ivar | infra ivar | dir |
|---|---|---|---|---|
| +0x00 | interface (`this`) | — | — | in |
| +0x08 | byte: number of TX submission queues (max. 7; `ud2` if ≥ 8) | +945 | +42 | in |
| +0x10 | u32[]: WME AC of each TX queue | +948 | +44 | in |
| +0x18 | `IOSkywalkPacketQueue*[]`: TX submission queues | +1032 | +120 | out |
| +0x20 | additional pointer (queue set) | +1024 | +112 | out |
| +0x28 | TX completion queue | +1008 | +96 | out |
| +0x30 | RX completion queue | +1016 | +104 | out |
| +0x38 | multicast queue (optional; 0 = not requested) | +1064 | +152 | out |
| +0x40 | TX pool | +1072 | +80 | out |
| +0x48 | RX pool | +1080 | +88 | out |
| +0x50 | u16 headroom | 64 (constant) | `reg+0x3C` | in |
| +0x58 | logger (`CCLogStream*`) | getLogger() | getLogger() | in |

`AppleBCMWLANPCIeSkywalk::registerSkywalkInterface` (`0xffffff8001549528`) validates `this`, TX queues, TX comp and RX comp non-null (error `0xE0000001`), reads `isLowLatencyEnabled` of the interface to choose the TX capacity (+24 normal / +28 low-latency of the bus), and creates:

1. **Multicast queue** (only if +0x38 ≠ 0): `AppleBCMWLANPCIeSkywalkMulticastQueue::withProviderAndQueueInfo(bus, interface, logger, fault)` (`0xffffff8001552e16`); 88-byte object wrapping an `AppleBCMWLANPCIeSkywalkFlowQueue` with flow key `0xFFFFFFFFFFFF` (all multicast).
2. **One `AppleBCMWLANPCIeSkywalkTxSubmissionQueue` per entry of the count byte** (`withProviderAndQueueInfoAndPool(bus, if, ifId, index, AC of the map, capacity, workqueue, pool, logger, fault)`; `0xffffff8001541122`), with `setInterface(interface)`; 376-byte object.
3. **TX completion** (`AppleBCMWLANPCIeSkywalkTxCompletionQueue`, same family factory) and **RX completion** (`AppleBCMWLANPCIeSkywalkRxCompletionQueue`, `0xffffff8001546478`; 328-byte object), both with `setInterface`.
4. The headroom of the descriptor is copied inside the pool (`*(u16*)(pool->tracker + 64) = headroom`).
5. **TX and RX pool = the same pointer** (descriptor +0x40 and +0x48): the common pool of the bus created in `allocSkywalkCommonResources`.
6. Marks the interface in the bus bitmap: `bus+44 |= 1 << ifId` (the same that `completeRegistration`, `0xffffff80015494c6`, does). Failure in any registration → rollback with release of what was created.

## 5. Pool and queues of the bus (sizes and options)

`AppleBCMWLANPCIeSkywalk::allocSkywalkCommonResources` (`0xffffff8001548d2a`), called by `initWithProviderAndCapacity`:

- `AppleBCMWLANPCIeSkywalkPacketPool::withName("AppleBCMWLANSkywalkPool", busif, logger, fault, &opts)` → bus+56. Options (`AppleBCMWLANSkywalkPoolOptions`, 24 bytes): `packetCount = bufferCount = bus+36`, `bufferSize = bus+40`, `maxBuffersPerPacket = 1`, `+0x10 = 0`, `flags (+0x14) = 1`, no DMA spec in PoolOptions (the PCIe pool adds its `IOMapper` separately, `setMapper`). The Broadcom base `AppleBCMWLANSkywalkPacketPool::initWithName` (`0xffffff800175c7ac`) copies those 24 bytes and calls the family `IOSkywalkPacketBufferPool::initWithName(name, owner, 1, opts)` — **same type (1) and same PoolOptions shape as the RTL layer already uses**; underneath, `kern_pbufpool_create` and segments (`segmentConstructor/Destructor`), with two `AppleBCMWLANHashtable` (TX/RX trackers) sized to the power of 2 of `packetCount`.
- Additionally it creates the refill queue of the bus itself: `AppleBCMWLANPCIeSkywalkRxSubmissionQueue` with capacity `bus+32` over the same pool, adds it to the work loop of the bus and enables it; and an `IO80211TimerSource` for `lowLatencyRingCreateTimeout`.
- `rxCompRingDrain` fills those RX buffers via `refillRxPacket` (`0xffffff8001550c18`, `?` internal detail not reviewed in this pass).

Bus capacities: `initWithProviderAndCapacity` (`0xffffff8001548bd6`) stores the arguments at +24/+28/+32/+36/+40 and `AppleBCMWLANBusInterfacePCIe::allocateSkywalkBusResource` (`0xffffff800158b85a`) computes them with defaults when the firmware configuration (dwords of the core at +4560/+4564/+4568/+4576) is zero. Observed defaults (the code itself logs them as `txLlwQueueSize txQueueSize rxQueueSize skywalkPacketPoolSize`):

| use | bus field | default |
|---|---|---|
| capacity of TX submission queue (and TX completion), AWDL | +24 | 512 (= `2 * txQueueSize` of the log … `?` exact argument-to-field correspondence not re-verified; the observed default TX value is 512) |
| capacity of low-latency TX queue | +28 | 1024 |
| capacity of RX completion (and RX submission queue of the bus) | +32 | 1024 |
| pool `packetCount`/`bufferCount` | +36 | 4864 (= tx + 4·txLlw + 2·rx + 256 with the defaults) |
| pool `bufferSize` | +40 | 2048 (0x800) |

Practical consequence: **all the queues of all the Broadcom interfaces share a pool of 4864 buffers of 2048 bytes**, and the effective AWDL MTU is the one of the `RegistrationInfo` (0x594 = 1428 usable after the 64 headroom).

## 6. PCIe queue slots the interface uses

The Broadcom subclasses extend the family vtable (the family one ends in the `_RESERVEDIOSkywalk…`). Broadcom slots (from the vptr of the object, resolved in the dumped PCIe vtables):

TX submission (`AppleBCMWLANPCIeSkywalkTxSubmissionQueue`, vptr `0xffffff80015acd00`):

| offset | method |
|---|---|
| +0x150 / +0x158 | family: `IOEventSource` enable/disable (via `IOSkywalkTxSubmissionQueue::enable/disable`) |
| +0x298 | family: `IOSkywalkTxSubmissionQueue::requestDequeue(void*, uint)` |
| +0x308 | `AppleBCMWLANSkywalkTxSubmissionQueue::initWithPoolAndQueueInfo` |
| +0x310 | `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets(OSObject*, IO80211NetworkPacket**, uint, void*)` |
| +0x318 | `…::forwardPacket(IO80211NetworkPacket*)` |
| +0x320 | `…::dupAndTransmitMcastPacket(pkt, uchar*, uchar*)` |
| +0x328 | `…::freeMulticastPacket(IO80211NetworkPacket*)` |
| +0x330 | `…::triggerAWDLMulticastTx(uint, uchar)` |
| +0x338 | `…::collectQueueStats(char*, ulong, ulong, ulong)` |
| +0x340/+0x348 | `…::getRingFreeSpace` / `getPendingPacketCount` |
| +0x350/+0x358 | `…::clearFWRings` / `clearFWRing` |
| +0x368 | `…::p2pMulticastTx(IO80211NetworkPacket**, uint)` |

RX completion (`AppleBCMWLANPCIeSkywalkRxCompletionQueue`, vptr `0xffffff80015ad9d8`):

| offset | method |
|---|---|
| +0x150 / +0x158 | family: enable/disable |
| +0x298 | family: `IOSkywalkRxCompletionQueue::requestEnqueue(void*, uint)` |
| +0x2A0 / +0x2A8 | family: `enqueuePackets(IOSkywalkPacket* const*, uint, uint)` / single-packet variant |
| +0x320 | `AppleBCMWLANSkywalkRxCompletionQueue::setInterface` |
| +0x328 | `AppleBCMWLANPCIeSkywalkRxCompletionQueue::collectQueueStats` |
| +0x330 | `…::flushPackets(void)` |
| +0x340 | `…::stagePacket(IO80211NetworkPacket*, bool, bool)` |
| +0x348 | `…::freePendingPackets(IO80211NetworkPacket**, uint)` |

TX completion (`AppleBCMWLANPCIeSkywalkTxCompletionQueue`): +0x320 `collectQueueStats`, +0x328 `getQueueId`, +0x330 `getLogger`.

## 7. Datapath getters (overrides)

All read ivars (own block of 0x460 bytes at object+0x138):

- `getRxCompQueue` (`0xffffff800172a3c8`, slot +0xD70) → `ivars+1016`.
- `getTxCompQueue` (`0xffffff800172a3b4`, +0xD78) → `ivars+1008`.
- `getTxPacketPool` (`0xffffff800172a3a0`, +0xD88) → `ivars+1072` (the common pool of the bus).
- `getTxSubQueue(wme_ac)` (`0xffffff800172a372`, +0xD80): `id = ivars+1088[4*ac]` (filled in start from `getQueueId`); if `id >= ivars+945` returns null; else `ivars+1032[8*id]`. That is, AC table → queue-id → queue.
- `getRxPacketPool` is **not overridden**: Proxi inherits `IO80211SkywalkInterface::getRxPacketPool` (slot +0xD90, `0xffffff80022f98ec`); its behavior for this interface was not decompiled (`?`).
- `getLogger` (+0xD08) → `ivars+64` (the CCStream the base created in start).
- `isChipInterfaceReady` (+0xB78) → `ivars+932 != 0`.

## 8. `enableDatapath` / `disableDatapath`

`enableDatapath` (`0xffffff8001729afc`, slot +0xD98):

1. If `ivars+932 == 0` (chip not ready): log "awdl interface is not ready, cannot enable data path yet" and **returns 0** (not an error).
2. If `isInterfaceEnabled()` (slot +0xCF0) is false: log `"%s enableDatapath failed because interface has not been enabled yet"` and returns `0xE00002BC`.
3. Enables TX completion (`IOEventSource` enable, slot +0x150), enables RX completion (+0x150), and for each TX submission queue (while there is no null pointer): enable (+0x150) and `requestDequeue(0, 0)` (family, +0x298) — the "start" of the dequeue service. Null pointer mid-way → log "tx submission queue ac %d not valid" and `0xE00002BC`.
4. If the multicast queue exists (`ivars+1064`): `IO80211WorkQueue::addWorkSource(wq, ws)` (gets the work queue via `getController` slot +0xBA8 → core slot 397), enable (+0x150) and call +0x1D0 on it (it initializes it as work source; `?` exact name).
5. `requestEnqueue(0, 0)` (family, +0x298) on RX completion; if it returns an error, info log "skywalk rx completion queue enqueue fail with return %d" (does not propagate the error).

`disableDatapath` (`0xffffff8001729ec6`, +0xDA0): disables in reverse order: TX submission queues (disable +0x158), multicast queue (disable + `IO80211WorkQueue::removeWorkSource`), RX completion, TX completion. Everything disabled → 0; something null → `0xE00002BC` with log `"%s: skywalk data path disable fail"`.

Life-cycle context observed in Broadcom (and already replicated in the RTL layer): the family does not enable the queues of the driver; it is the driver itself that calls `enableDatapath` from `enable(unsigned)` (slot +0x850: first the base `IO80211VirtualInterface::enable` via the imported vtable, then slot +0xD98) and `disable(unsigned)` (+0x858 → base + slot +0xDA0). `setInterfaceEnable(bool)` (+0x930) calls the base and, if the interface is up (`ivars+932 != 0`), reports link with `IOSkywalkNetworkInterface::reportLinkStatus(this, on ? 3 : 1, on ? 128 : 130)`; else, "AWDL interface not up yet, not enabling queues".

## 9. TX: from the stack to the chip

1. The stack enqueues into the registered TxSubmissionQueue (the family `IOSkywalkTxSubmissionQueue` manages the Skywalk ring and the credit).
2. The action of the queue is `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets` (slot +0x310; `0xffffff80015420f0`): per packet it does `safeMetaCast` to `AppleBCMWLANPCIeSkywalkPacket` (the packets of the PCIe pool) and checks `isInterfaceEnabled` (+0xCF0) and "chip ready" (slot +0xB78 of the interface); interface not enabled → `0xE00002D8`, invalid interface or chip not ready → `0xE0000001`.
3. Split by role of the interface: role 1 → `dequeueInfraPacket` (flow queue of the current AP), roles 2/softAP/LLW → `dequeueSoftAPLLWPacket` (table of up to 16 flow queues per destination MAC, on-demand creation with `AppleBCMWLANPCIeFlowQueue::withProviderAndQueueInfo`), and **roles 6 (AWDL) and 7–10 (NAN) → `dequeueAWDLNANPacket`**:
   - link multicast: `AppleBCMWLANPCIeSkywalkPacketPool::duplicatePacket` and `IO80211VirtualInterface::stashMulticastPacketPair(if, pkt, dup)` — the original+duplicate pair is stored in the virtual interface to transmit over the second social channel (flag 0x80 on the duplicate); without a valid virtual interface → error `0xE00002C0` (branch "Invalid Virtual Interface for AWDL !").
   - unicast: insertion of a Broadcom SNAP header (pattern observed in the packet: `AA AA 03 … 17 8F 08`, constants `0x0008F2170003AAAA`, 12-bit counter at +24 combined with the ring id `<< 12`), headroom/`setLinkHeaderOffset` adjustment, notification to the interface (slot +0x918) with the pointer of the header, and `findOrCreateFlowQueue` (slot +0xC70 of the interface) → `AppleBCMWLANPCIeFlowQueue::enqueuePacket(fq, pkt, true)`.
   - If there is no flow queue nor room (max. 16 per queue, counter in queue ivars +168): error and discard.
4. The `AppleBCMWLANPCIeFlowQueue` are tied to firmware flow rings (`attachTxSubmRing` of the bus, `AppleBCMWLANPCIeTransmitFlowRing`); at the end of the batch, if there are pending ones `signalTxPendingFlowQueue` is signaled, and if not the rings of the flows with traffic are kicked (call +0x230 of the ring with `(id, 1)`).
5. AWDL/multicast specific ones, all delegating into the **first** TX submission queue (`ivars+1032[0]`):
   - `triggerAWDLMulticastTx(uint, uchar)` (+0xF10): finds the multicast flow queue with `IO80211VirtualInterface::findExistingFlowQueue(ifId | 0xFFFF0000)` (slot +0xC78) and kicks its ring; not found → `0xE00002C7`.
   - `p2pMulticastTx(IO80211NetworkPacket**, uint)` (+0xF18): direct call to `dequeuePackets(this, packets, n, 0)` — processes the list like the normal dequeue.
   - `freeMulticastPacket(IO80211NetworkPacket*)`: clears the flag of the packet and `AppleBCMWLANPCIeSkywalkPacketPool::deallocatePacket`.
6. The TX completion queue (`ivars+1008`) is the one the family uses to return the packets to the pool; the firmware→comp queue drain is the bus's own (`txCompRingDrain`, not decompiled in this pass, `?`).

## 10. RX: from the chip to the stack

`AppleBCMWLANPCIeSkywalk::rxCompRingDrain(ring, ctx, msg, n)` (`0xffffff8001549dea`), per message of the RX completion ring:

1. Locates the destination interface (by interface id of the message or `core→getInterfaceById`, slot 632; for role 6 it uses `getProximityInterfaceId` of the core).
2. Processing for AWDL (role 6): discards < 28 bytes ("Too small (len %d)"), processes optional VLAN (strip 4), and detects SNAP at +14 (`AA AA 03`): if the OUI/ethertype matches its own (comparison against `0x08F20800`) it removes 16 bytes; if OUI 0 it removes 8 bytes and redirects to the LLW interface (`getInterfaceById(core, 9)`); without SNAP it requires ethertype ≥ 0x600. Headroom/`setLinkHeaderOffset` and length adjustments.
3. Pre-hook of the interface: call to slot +0x8F8 of the interface with `(packet, data, len, &flag, 0)`; return 0 → discard (returns the packet to the pool).
4. Gets the destination queue with the **slot +0xD70 (`getRxCompQueue`) of the interface**, `safeMetaCast` to `AppleBCMWLANPCIeSkywalkRxCompletionQueue`; if it is not of that class → "Invalid getRxCompQueue(), go next" and discard. Sets RX timestamp (`setRXTimestamp`) and delivers the packet to that queue with a three-argument call `(packet, 0, 0)` through its vtable (`?` the decompiler does not name the slot; the family exposes `enqueuePackets` of a single packet at +0x2A8 and it is the only submit of the class besides the Broadcom +0x320–0x348).
5. Discards and errors return the packet to the pool (`AppleBCMWLANSkywalkPacketPool::freePacket`, `?` exact name of the thunk `0xffffff80017fa328`).
6. At the end of the batch: `AppleBCMWLANBusInterfacePCIe::scheduleRxCompleteLegacy` (refill), and for **the 12 possible interfaces** (`getInterfaceById(core, 0..11)`): `requestEnqueue(0, 0)` (+0x298) on their `getRxCompQueue` and, if it fails and the role is not 1, `flushPackets` (+0x330). Then `updateDataPathActivityTimestamp`/`handleDataPathActivity` of the core.

That is: the frame reaches the stack because the bus enqueues it **into the same RxCompletionQueue the interface registered in Skywalk** — there is no second copy nor conversion to mbuf anywhere in the Broadcom kext path.

## 11. Pending, flush and diagnostics

- `sendPendingPacketsToStack(IO80211NetworkPacket**, uint)` (+0xF30, `0xffffff800172a716`): with a valid RX completion, per packet `stagePacket(pkt, 0, 0)` (+0x340), then `requestEnqueue(0, 0)` (+0x298) and, if it returns an error, `flushPackets()` (+0x330). Without a queue → "FAIL : Invalid Rx Completion Queue available". It is the mechanism with which the interface delivers to the stack the held packets (e.g. the stashed multicast when the second channel opens).
- `freePendingPackets(IO80211NetworkPacket**, uint)` (+0xF38): delegates into the RX queue: `freePendingPackets(packets, n)` (+0x348), which returns the packets to the pool.
- `flushFlowQueues(ether_addr*)` (+0xED8, `0xffffff800172ac9e`): calls `AppleBCMWLANSkywalkInterface::flushFlowQueues(companion, mac)` on the pointer at `ivars+120` (the low latency Skywalk companion interface, an `AppleBCMWLANLowLatencyInterface` role 2 created by `AppleBCMWLANCore::createSkywalkInterface` via the ioctl `setCOMPANION_SKYWALK_IF_CREATE`; that function per MAC peer walks the TX submission queues and calls slot +0x358, "remove flow queue of the peer"), and afterwards the base `IO80211VirtualInterface::flushFlowQueues` (`0xffffff800233235e`), which on 25.6.0 is **empty** (no-op).
- `printDataPath(userPrintCtx*)` (+0xC58): `collectQueueStats` (+0x338) of each TX submission queue, of the TX completion (+0x320) and of the RX completion (+0x328), with headers "Dumping TxSubQueue:%u status", "Logging TX Completion Queue Status", "Logging RX Completion Queue Status".
- `setCOMPANION_SKYWALK_IF_CREATE` (`0xffffff8001722ef2`): request+4 = 1 creates the companion if it does not exist (log "already exists" if there already is one; writes its BSD name into request+8, max. 16 bytes) and setCompanionInterfaceId; request+4 = 0 destroys it (`destroySkywalkInterface`) or returns `0xE00002C0` if there is none.

## 12. `setMacAddress`, `stop`, `free`

- `setMacAddress(ether_addr&)` (+0xF40, `0xffffff8001720354`): it refuses (`0xE00002D5`) if the AWDL synchrony is active (`ivars+672 != 0`) or there is a chip interface creation in progress (`ivars+1112 & 1`), with log "Cannot update MAC address because sync state: %s CreatingChipIF %s". Otherwise: if flag `ivars+128 & 1` is active it sends the `cur_etheraddr` iovar through the `AppleBCMWLANCommander` (`runVirtualIOVarSet`, error "cur_etheraddr failed"); afterwards it notifies the bus: bus interface of the core → virtual +0x1F8 → `safeMetaCast AppleBCMWLANBusSkywalk` → virtual call with the interface as argument (`?` the exact slot within the vtable of the bus was not identified due to the typing of the decompiler; by symbols there is a `setMacAddress` in the family of interfaces, not on the bus).
- `stop(IOService*)` (+0x5C8, `0xffffff80017195e6`): if there is a valid work loop and it is not already on it, it re-enters via `IOWorkLoop::runActionBlock` (the block calls its own slot +0x5C8). On the teardown path: for each TX submission queue, TX completion, RX completion and the multicast/work source: `IOEventSource::disable` (+0x158), `IO80211WorkQueue::removeEventSource` (+0x148), `release` (+0x28) and slot to null; for the work source additionally `removeWorkSource`. Then, qualified call to the base `stop` (+0x5C8 of the vtable of `IO80211AWDLProtocol`). It does not touch the pools (the bus owns them).
- `free` (`0xffffff8001719582`): `freeResources` (own slot +0x11D0), `bzero` of the 0x460 bytes of ivars + `IOFreeType`, and base `free` (+0x90).

## 13. `getBSDNamePrefix` and the BSD name

`getBSDNamePrefix` (`0xffffff8001715a7c`, slot +0x9E8) does `IO80211SkywalkInterface::validateDispatchQueue(this)` and returns `&0xffffff8001797376`, whose content is the literal `"awdl\0"` (verified by reading the bytes in the KC). The same pointer is the one `createProxInterface` passes as third argument of `withOptions` (the name that reaches `init`) and the one stored at `RegistrationInfo+0x30` (field whose default in `initRegistrationInfo` is "en"). The final name is built by `IO80211SkywalkInterface::getBSDName` (`0xffffff80022f5fca`): it caches into base ivars+1 (16 bytes) `snprintf("%s%u", ifnet_name(ifnet), ifnet_unit(ifnet))`, with the ifnet obtained via slot +0x8F8 of the interface; hence `awdl0`. The prefix, therefore, enters the ifnet through the `RegistrationInfo`, not through a separate call.

## 14. Comparison with RTL8814AirLayer (current infrastructure interface)

What the layer already does (source: `Source/AirLayer/RTLAirLayer.{hpp,cpp}`, read only) versus what AWDL requires:

| piece | AirLayer today | AWDL Broadcom | reusable / new |
|---|---|---|---|
| Base interface class | `IO80211InfraProtocol` (via `RTLAirInterfaceStubs`) | `IO80211AWDLProtocol` (subclass of `IO80211VirtualInterface`) | **New**: AWDL subclass with ~63 pure slots of the base to implement (`Docs/AWDL_PROTOTYPE_DESIGN.md` §3) |
| `init` | `init()` + `IO80211SkywalkInterface::init` + `setInterfaceRole(1)`/`setInterfaceId(1)` + `attach` | identical in shape, with role 6, `setInterfaceId(2)`, Taskmaster and ~20 state-machine ivars | Reusable the skeleton; new the Taskmaster and the AWDL state |
| Pools | creates 2 own pools (`withName("rtlair:tx"/"rtlair:rx", this, 1, opts)`; 512×2048, maxBuffersPerPacket 1, flags 1) | 1 shared pool of the bus ("AppleBCMWLANSkywalkPool", 4864×2048, same PoolOptions shapes) | Reusable the mechanism (`withName` + kern_pbufpool); new only the size/ownership (one pool, shared with other interfaces if a custom bus existed) |
| Queues | 3: TxSubmission (cap 256), TxCompletion (256), RxCompletion (256), with own action on TX | 4 TX submission (one per AC, bus capacity 512), TX comp, RX comp (1024), optional multicast; created by the bus | Partially reusable: creation by family factory is the same; new the 4×AC + multicast model and the fact that Broadcom creates them on the bus, not the interface |
| Registration | `IO80211InfraInterface::registerInfraEthernetInterface(reg, {txsub,txcomp,rxcomp}, 3, txPool, rxPool)` | `IOSkywalkEthernetInterface::registerEthernetInterface` direct, after `registerSkywalkInterface` of the bus | Reusable the ABI knowledge; for a custom AWDL interface the direct registration against the family is the path observed in role 6 |
| `RegistrationInfo` | version 1/0x130, +0x0C=3, +0x10=1, +0x14=2, +0x24=0x8000000080, headroom 64 at +0x3C, flags 0 at +0x40, MAC at +0x108 | equal up to +0x24; no +0x10; +0x30="awdl", +0x3A=64, +0x40=4/6, +0x48/+0x4C=0x594 | Almost everything reusable; **new** prefix at +0x30 and MTU 0x594; headroom offset discrepancy (+0x3A vs +0x3C) to resolve before copying |
| Queue enabling | `enable/disable` (slots 266/267) spin the 3 queues on the work queue (`IOEventSource`) | `enable/disable` (+0x850/858) → `enableDatapath/disableDatapath` (+0xD98/0xDA0): same pattern + `requestDequeue/requestEnqueue(0,0)` + work source | Reusable; new the multicast work source and the (0,0) calls |
| TX | own action: copy to mbuf → radio ABI → immediate completion by chunks of 64 | action of the PCIe subclass: SNAP + flow queue per destination + firmware flow ring; duplicated multicast with stash | **New** almost the whole model (flow queues/per-destination does not exist in the layer); the part "consume packet from the queue and return it via the completion" is the same contract |
| RX | radio hook → mbuf staging (512) → thread call → `allocatePacket` + copy + `enqueuePackets` by 64 | ring drain → `enqueuePackets` direct into the registered RX comp (zero copies) | Reusable the `enqueuePackets` contract (incl. `prepare` without source queue and batches < capacity); new the rest (refill, rings) |
| Pending/flush | do not exist (synchronous TX completion) | `stagePacket`/`freePendingPackets`/`flushPackets` + `flushFlowQueues` (delegated into the companion) | **New** |
| Multicast | nothing (no filter or queue) | multicast queue + `triggerAWDLMulticastTx` + `p2pMulticastTx` + `freeMulticastPacket` + duplicate for the second channel | **New** (it is the specifically AWDL part of the datapath) |
| `getBSDNamePrefix` | not overridden (naming by InterfaceNamer; MAC with local bit to force own unit) | returns "awdl" and goes into `RegistrationInfo+0x30` | **New** and trivial: a literal + the registration field |
| `setMacAddress` | log and return 0 (the agent of the family moves the interface; syncs radio with `RTL_RADIO_FN_SET_MAC`) | sync/chip-IF gates + `cur_etheraddr` iovar via Commander + notice to the bus | Reusable the agent part; new the gate and the iovar (in RTL it does not apply: the radio sets the MAC) |
| `stop`/`free` | disable+removeEventSource+release per queue, then base; `quiesce` on the controller | identical in order (disable, removeEventSource, release, removeWorkSource, base) | Reusable as-is |
| Peer manager / WCL Glue | WCL glue travels with role 1 of the family (already active in the STA layer) | no glue; own peer manager of the interface (created by the base start) | New for AWDL (accept its peer manager and the AWDL requests of `wifip2pd`) |

In synthesis: from the AWDL datapath what is reused directly is the `init/attach` cycle, the construction of the `RegistrationInfo`, the pool/queue factories of the family, the addEventSource/enable/disable pattern over the `IO80211WorkQueue`, the registration against Skywalk and the teardown; what is genuinely new for a custom role 6 is (1) the base class `IO80211AWDLProtocol` with its pure slots, (2) the model of 4 queues per WME AC with the AC→queue-id map, (3) the multicast pair (stash/duplicate, `triggerAWDLMulticastTx`, `p2pMulticastTx`), (4) the pending packets (`stagePacket`/`freePendingPackets`/`flushPackets`) and the flow queue flush, (5) the "awdl" prefix + MTU 0x594 in the `RegistrationInfo`, (6) Taskmaster/Commander/peer manager of its own, and (7) the integration with `AppleBCMWLANCore::setAwdlInterface` and the later AWDL requests. The numeric capacities of queues (512/1024) and pool (4864×2048) are Broadcom defaults tied to their firmware; a custom driver can choose others while respecting the observed invariants (batch of `enqueuePackets` < capacity of the queue; packets from the same pool as the queue).

## 15. Not verified in this pass

- Behavior of `IO80211SkywalkInterface::getRxPacketPool` (slot +0xD90, inherited by Proxi) and of the `IOMapper` of the PCIe pool.
- Exact slot of the bus that receives the `setMacAddress` notice and the exact name of the `freePacket` thunk (`0xffffff80017fa328`).
- Argument-to-field correspondence of `withProviderAndCapacity` (the defaults 512/1024/4864/2048 come from the computation of `allocateSkywalkBusResource` and from its own log; the fine assignment +24/+28/+32/+36/+40 is deduced from the use in `registerSkywalkInterface`/`allocSkywalkCommonResources`).
- Semantics of argument 2 of `registerService` on the success of `start` (infra uses 8).
- `txCompRingDrain`, `refillRxPacket`, `signalTxPendingFlowQueue` and the inside of `AppleBCMWLANPCIeFlowQueue` (firmware rings): cited by symbols and observed calls, without full pseudocode.
- Everything marked `?` above. None of the conclusions implies a test on RTL8814 hardware.
