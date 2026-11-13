# Teardown of the `IO80211AWDLPeerManager` of an external-driver `awdl0` (role 6)

Closes the `?` of [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md) §8 (who releases
`ivars+0x28`) and details the full destruction cycle: what the manager releases or cancels, in what
order relative to the `stop` of the interface and the BSD deregistration, which paths call the driver and with
what locks. Base: KC 25G241 (macOS 26.7.1) in IDA. Unverified items go with `?`.

Summary of the chain (all addresses verified in disassembly or pseudocode; §12):

1. The driver calls the controller slot **+0xDE8** `detachVirtualInterface(vif, sync)`
   (`0xffffff8002296116`), which invokes slot **+0x600** (`terminate`) of the vif with
   `options = 2*sync+1` ∈ {1,3} (§2).
2. `IO80211VirtualInterface::terminate` closes the vif on the controller
   (`IOService::close`), removes the interested drivers, releases the BPF list and chains
   `IOService::terminate` → `willTerminate` → `didTerminate` → `stop` (§3).
3. `IO80211VirtualInterface::stop` releases the BPF resources, asks for
   `disableVirtualInterface` on the controller (base: returns 0 without touching the driver) and calls
   `IO80211SkywalkInterface::stop` (§4).
4. **`IO80211SkywalkInterface::stop` (`0xffffff80022f885c`) is who releases the manager** for
   role ≠ 2: it calls `IO80211PeerManager::stop` (slot +0x290) and then `release()` on
   `ivars+0x28`, and zeroes the pointer for all roles (§5). With the retains balanced
   (§7), that `release` brings the counter to 0 and `IO80211AWDLPeerManager::free` runs
   synchronously on the same thread.
5. `IO80211PeerManager::stop` = `removeAllPeers` (slot +0x280) + `freeResources`
   (slot +0x288, tail-jmp): peer deletion —with callback +0xF38 to the driver— and deactivation
   of timers and queues (§6).
6. The BSD deregistration (`deregisterNetworkInterface`) happens **after** the release of the manager,
   within the same `swi_stop` (§5.3). None of this runs under the controller workloop/gate:
   it is the IOKit termination thread; the manager takes its own data-path lock within
   `removeAllPeers` (§8).

## 1. Vtables and slots involved

| Class | vptr | Slot | Target |
|---|---|---|---|
| `IO80211SkywalkInterface` | `0xffffff800246af10` | +0x090 `free` | `0xffffff80022f6184` |
| | | +0x400 `willTerminate` | `IOSkywalkNetworkInterface::willTerminate` `0xffffff8002b0bfea` |
| | | +0x408 `didTerminate` | `IOSkywalkInterface::didTerminate` `0xffffff8002afbe12` |
| | | +0x5C8 `stop` | `0xffffff80022f885c` |
| | | +0x600 `terminate` | `IOService::terminate` `0xffffff8000a937b0` |
| | | +0x678 `detach` | `IOService::detach` `0xffffff8000a94670` |
| `IO80211VirtualInterface` | `0xffffff8002472b88` | +0x090 `free` | `0xffffff8002330b8c` |
| | | +0x5C8 `stop` | `0xffffff8002330cee` |
| | | +0x600 `terminate` | `0xffffff8002330c50` |
| | | +0x678 `detach` | `0xffffff800233027c` |
| | | +0xF38 `freePendingPackets` | base `0xffffff8002334e88` (log only) |
| `IO80211PeerManager` | `0xffffff800245d080` | +0x090 `free` | `0xffffff80022492f6` (cell `0xffffff800245d110`) |
| | | +0x190 `destroyReporters` | `0xffffff80022496ae` |
| | | +0x258 `disableWorkQueueSources` | `0xffffff80022497e6` |
| | | +0x278 `removePeer(Peer*)` | `0xffffff8002254096` (cell `0xffffff800245d2f8`) |
| | | +0x280 `removeAllPeers` | `0xffffff80022542e4` (cell `0xffffff800245d300`) |
| | | +0x288 `freeResources` | `0xffffff8002249000` (cell `0xffffff800245d308`) |
| | | +0x290 `stop` | `0xffffff80022492d2` (cell `0xffffff800245d310`) |
| `IO80211AWDLPeerManager` | `0xffffff800245a868` | +0x090 `free` | `0xffffff80021c4aae` (cell `0xffffff800245a8f8`) |
| | | +0x160 `interfaceMonitor` | `0xffffff80021ed628` |
| | | +0x190 `destroyReporters` | `0xffffff80021cc87e` |
| | | +0x278 `removePeer` | `0xffffff800221721c` (cell `0xffffff800245aae0`) |
| | | +0x280 `removeAllPeers` | `0xffffff800222be56` (cell `0xffffff800245aae8`) |
| | | +0x288 `freeResources` | `0xffffff80021c48b0` |
| | | +0x290 `stop` | `0xffffff80021c487a` (cell `0xffffff800245aaf8`) |
| `IO80211Controller` | `0xffffff8002461158` | +0x5D8 `close` | `IOService::close` `0xffffff8000a935c0` |
| | | +0x850 `executeCommand` | `IONetworkController::executeCommand` `0xffffff80026f51e0` |
| | | +0xC68 `getWorkQueue` | `0xffffff8002295e28` |
| | | +0xD10 `disableVirtualInterface` | `0xffffff80022965bc` |
| | | +0xD88 `allocIO80211RecursiveLock` | `0xffffff800229a99a` |
| | | +0xDE8 `detachVirtualInterface` | `0xffffff8002296116` (cell `0xffffff8002461f40`) |
| `IO80211Peer` | `0xffffff800245c748` | +0x158 `stop` | `0xffffff800223f858` |
| | | +0x200 `flushQueues(uint)` | `0xffffff8002240ea0` |

The destructors of both managers are trivial (only `OSObject::~OSObject` + typed `operator delete`:
`pm_D1` `0xffffff8002246342`, `pm_D0` `0xffffff800224634c` with `delete` of 32 B,
`awdl_D1` `0xffffff80021c1376`, `awdl_D0` `0xffffff80021c1380` with `delete` of 40 B). All the
real teardown lives in `free` (+0x090), `freeResources` (+0x288) and `disableWorkQueueSources`
(+0x258).

## 2. Entry point from the driver

`IO80211Controller::detachVirtualInterface(IO80211VirtualInterface*, bool)`
(`0xffffff8002296116`, slot **+0xDE8**): loads `esi = 2*bool+1` at `0xffffff800229611d`, reads
slot +0x600 of the vif at `0xffffff8002296124` and jumps (`0xffffff800229612c`). That is, a
call to `vif->terminate(1)` (synchronous) or `terminate(3)`. There is no caller of this function
in the KC (only the controller vtable cells: the family `0xffffff8002461f40` and the Broadcom one
`0xffffff80017ea5e0`): it is the kext entry gate.

`IO80211VirtualInterface::terminate` (`0xffffff8002330c50`):

1. Marks byte `ivars_vif+0x52` = 1 (termination flag; ivars of the vif at `this+0x128`).
2. Takes the provider with slot +0xBA8 (`getController`) and calls its slot **+0x5D8**
   (`call qword ptr [rcx+5D8h]` at `0xffffff8002330c7e`) with `(controller, vif, options)` =
   `IOService::close`. In the base vtable of `IO80211Controller` that cell is `IOService::close`
   (`0xffffff8000a935c0`): a `close` override of the RTL controller would receive the call here.
3. `IOService::deRegisterInterestedDriver(provider, vif)` (`call` at `0xffffff8002330c96`).
4. Releases the BPF list (`IO80211BPFListFree`, `call` at `0xffffff8002330cab`; the block of
   `ivars_vif+0x28` with `IOFreeTypeImpl` at `0xffffff8002330cc9`).
5. Tail-jmp to `IOService::terminate` (`0xffffff8002330ce8` → `0xffffff8000a937b0`), which launches
   `terminatePhase1(options|4)`. The IOKit machinery then calls `willTerminate` (slot
   +0x400: `IOSkywalkNetworkInterface::willTerminate` `0xffffff8002b0bfea`, which removes the
   non-default logical links and the steering rules, and chains
   `IOSkywalkInterface::willTerminate` `0xffffff8002afbdfe`, which sets the termination byte of
   its ivars block), `didTerminate` (+0x408, `0xffffff8002afbe12`, inherited without override in
   either interface) and finally `stop` (+0x5C8).

The plane detach (`IO80211VirtualInterface::detach`, `0xffffff800233027c`) only does
`detachFromParent` on plane `kIO80211PlaneName` + `IOService::detach`: it runs in the final
IOKit phase, not a driver path.

## 3. `IO80211VirtualInterface::stop` (`0xffffff8002330cee`)

1. Log `"IO80211VirtualInterface::willTerminate: called, vif %s\n"` (enabled depending on byte
   `ivars+0x50`, test at `0xffffff8002330d10`).
2. `ivars+0x52 = 1` (`0xffffff8002330d2d`).
3. `freeBpfResources` (`call` at `0xffffff8002330d34`).
4. If the vif was enabled (bit 0 of `ivars+0x50`): `getController` (slot +0xBA8,
   `0xffffff8002330d4c`) and **`disableVirtualInterface`** (slot +0xD10 of the controller,
   `call` at `0xffffff8002330d5b`). The base `IO80211Controller::disableVirtualInterface`
   (`0xffffff80022965bc`) is a `return 0`: it does not reach the driver. Afterwards `ivars+0x50 = 0`
   (`0xffffff8002330d78`).
5. Tail-jmp to `IO80211SkywalkInterface::stop` (`0xffffff8002330d86`).

## 4. `IO80211SkywalkInterface::free` does not release the manager

`IO80211SkywalkInterface::free` (`0xffffff80022f6184`) does `release` of ivars `+0xD8`,
`+0xB8`, `+0xA8`, `+0x40`, `+0x48`, `+0x18`, `+0xC8`, `+0x30`, `+0x38`, `memset_s` of the
0xF0 B block (`call` at `0xffffff80022f631b`) and `IOFreeTypeImpl` (`0xffffff80022f6335`). **It does not
touch `+0x28`**: when `free` runs, the pointer is already 0 and the object no longer exists (the
`stop` guarantees it, §5).

## 5. `IO80211SkywalkInterface::stop` (`0xffffff80022f885c`): the point that releases the manager

Base ivars = `*(this+0x120)`. Verified order:

1. `IOService::PMstop` on the vif (slot +0x798, `call` at `0xffffff80022f889e`).
2. Object of `ivars+0x80` (log pipe): slot +0x870 (`call` at `0xffffff80022f88ba`) and
   `release` (`0xffffff80022f88d6`); `release` of `ivars+0x78` (`0xffffff80022f88f7`) and of
   `ivars+0x50` (`0xffffff80022f8915`), with `ivars+0x50 = 0` at `0xffffff80022f891f`.
3. Role test: `cmp dword ptr [rax+58h], 2` at `0xffffff80022f8927`; if the role is 2 (P2P, which
   inherits the manager from the parent) it jumps straight to `0xffffff80022f89a5`. For the other roles
   (including 6):
   1. `IO80211Peer::stop` (slot +0x158) on the broadcast peer of base `ivars+0x530`
      (`call` at `0xffffff80022f8947`) and on the unicast peer of `ivars base+0x538`
      (`call` at `0xffffff80022f896e`). Base `ivars+0x530` comes from `mov rax, [rcx+530h]` at
      `0xffffff80022f8935` with `rcx = manager+0x18`.
   2. **`manager->stop()`** (slot +0x290, `call qword ptr [rax+290h]` at `0xffffff80022f8982`).
   3. **`manager->release()`** (slot +0x28, `call qword ptr [rax+28h]` at `0xffffff80022f899b`).
4. **`ivars+0x28 = 0`** for all roles (`mov qword ptr [rax+28h], 0` at
   `0xffffff80022f89a5`).
5. Direct `IO80211Glue::stop` (`call` at `0xffffff80022f89b9` → `0xffffff80021939ac`) on the
   object of `ivars+0xD8`, if it exists.
6. Devirtualized call to slot +0x5D8 of the vtable of `IOSkywalkNetworkInterface`
   (`mov rax, cs:off_FFFFFF8002450128` at `0xffffff80022f89be` — the stored vptr is
   `__ZTV26IOSkywalkEthernetInterface`, `call qword ptr [rax+5D8h]` at `0xffffff80022f89cb`):
   it lands in `IOSkywalkNetworkInterface::stop` (`0xffffff8002b09e66`) (§5.3).
7. Final log of reference counters (`"IO80211SkywalkInterface::stop[%s] RC=<%d> core
   RC=<%d>\n"`; c-string with slot +0x148 at `0xffffff80022f89e7`, `getRetainCount` at
   `0xffffff80022f89fb` and `0xffffff80022f8a0f`).

This `stop` runs on the IOKit termination thread, without the controller gate and without the
data-path lock of the manager.

### 5.1 Closure of the `?` of AWDL_PEERMANAGER_CREATION §8

The release of `ivars+0x28` was not in `free` because it is in `stop`, with a role guard: the
manager is stopped and released in `IO80211SkywalkInterface::stop` (`0xffffff80022f8982` and
`0xffffff80022f899b`) and the pointer is nulled at `0xffffff80022f89a5`.

### 5.2 Slot +0x158 on the persistent peers

The two pseudo-peers of the base (`+0x530` broadcast, created with `IO80211Peer::withAddressAndManager`
on the broadcast address in `initWithInterface`; `+0x538` unicast) are stopped with
`IO80211Peer::stop` (`0xffffff800223f858`: releases the TX latency store of `ivars+0x4F0`,
does `release` of the object of `ivars+0x4B8`, calls its slot +0x200 with 0 and clears its bit of the
active-interface bitmap of the controller). Their definitive teardown is in
`freeResources` (§6.5).

### 5.3 BSD deregistration, after the manager

`IOSkywalkNetworkInterface::stop` (`0xffffff8002b09e66`): if the registration ivar is not null
(read in the first statement of the pseudocode), it calls
`IOSkywalkNetworkInterface::deregisterNetworkInterface` (`0xffffff8002b09e9c`) —removes logical
links and steering rules, does `IOSkywalkLogicalLink::setInterface(nullptr)`, releases the link
and its queues and releases the parameter block—; then it chains `IOService::stop`
(`0xffffff8000a93470`, no effect without IOUserServer). The order is: **manager stop + release →
glue stop → BSD deregistration**, all within `swi_stop`.

## 6. `IO80211PeerManager::stop` and the teardown of the manager

### 6.1 `IO80211AWDLPeerManager::stop` (`0xffffff80021c487a`)

If `this+0x20` (AWDL ivars) exists, it calls slot **+0x118** of the object at `AWDL ivars+0x1E0`
(`call qword ptr [rax+118h]` at `0xffffff80021c489b`): it is the `IO80211P2PSupervisor` created in
`initWithInterface`, and its slot +0x118 is `IO80211P2PSupervisor::stop` (`0xffffff8002390a46`),
which does `release` of the manager stored at `supervisor+0x10` and nulls the pointer — this release
balances the retain `initWithInterface` did when registering the pointer. Then tail-jmp to
`IO80211PeerManager::stop` (`0xffffff80021c48aa`).

`IO80211PeerManager::stop` (`0xffffff80022492d2`): `call qword ptr [rax+280h]`
(`removeAllPeers`) at `0xffffff80022492de` and tail-jmp to slot +0x288 (`freeResources`) at
`0xffffff80022492f0`.

### 6.2 `removeAllPeers` (`0xffffff80022542e4`)

1. `lockDataPath`: slot **+0x118** of the lock at `ivars base+0x78` (`call` at
   `0xffffff8002254309`; the object is created by `IO80211Controller::allocIO80211RecursiveLock`,
   slot +0xD88). On failure it logs "lockDataPath failed" and exits without deleting anything.
2. Walks the peer queue (`ivars base+0x540`) with `IO80211CommandQueue::map` (lea of
   `_removePeers` at `0xffffff8002254358`, `call` at `0xffffff800225436e`). `_removePeers`
   (`0xffffff800225443f`) only returns 12 (marks the entry for flush) and the map mechanism
   delivers each peer to `_addToFlush` (`0xffffff800224f6b0`), which enqueues it in the flush queue
   `ivars base+0x758`.
3. `IO80211Peer::flushQueues(0)` (slot +0x200) on `+0x538` (`call` at `0xffffff8002254388`)
   and `+0x530` (`call` at `0xffffff80022543a3`), with the lock taken.
4. `unlockDataPath` (slot +0x120, `call` at `0xffffff80022543b4`).
5. `setPeersCount` (`call` at `0xffffff8002254416`) and **`removeFlushedPeers`** (`call` at
   `0xffffff800225441e`), already **without** the data-path lock.

`removeFlushedPeers` (`0xffffff800224f73a`) dequeues from `+0x758` with
`IO80211CommandQueue::lockDequeue` (`call` at `0xffffff800224f755`) and calls slot **+0x278**
of the manager (`call qword ptr [rcx+278h]` at `0xffffff800224f768`) for each peer: the real deletion
—and the callbacks to the driver— run outside the data-path lock.

### 6.3 `IO80211AWDLPeerManager::removePeer` (`0xffffff800221721c`) per peer

1. Clears the states referencing the peer (sidecar, AirPlay, timesync, high-thruput, RTG,
   ranging, common channel: `updateAWDLCommonChannel(false)` at `0xffffff80022177dc`).
2. **`IO80211AWDLPeer::flushPacketsWaitingForPPP`** (`call` at `0xffffff80022177e4`):
   empties the PPP queue of the peer in batches of 64 (`dequeuePPPWaitPacketFromHead`, `call` at
   `0xffffff80021bff50`) and delivers each batch to **slot +0xF38 of the vif**
   (`call qword ptr [rax+0F38h]` at `0xffffff80021bff87`). Array of 512 B (`IOMallocZeroData`),
   freed with `IOFreeData` by the function itself; the packets become the slot's property.
3. `pppDone(false)` (`call` at `0xffffff80022177ee`, with `esi = 0` at `0xffffff80022177ec`).
4. **`IO80211SkywalkInterface::detachPeer`** (slot **+0xDF8** of the vif): loads the vif from
   `ivars base+0x560` (`0xffffff80022177f7`) and the peer MAC (`common ivars+0x58`) and calls
   `call qword ptr [rax+0DF8h]` at `0xffffff8002217809`.
5. Tail-jmp to `IO80211PeerManager::removePeer` (`0xffffff8002217823` → `0xffffff8002254096`),
   which posts event 47 (`postMessageSync(0x2F, buf, 0x44)` at `0xffffff8002254248`, only if
   `ivars base+0x560` is not null), calls `IO80211Peer::stop` (slot +0x158 of the peer) and, if
   applicable, `deletePeerFromFWCache`, and ends with `release()` of the peer.

`IO80211Peer::free` (`0xffffff800223f96a`) frees the three ivars blocks of the peer, calls
`IO80211PeerManager::removePeerOperation` (counter) and does the **`release` of the manager** the peer
retained in its common ivars `+0x60`, before freeing the 0x508 B block.

### 6.4 `IO80211AWDLPeerManager::freeResources` (`0xffffff80021c48b0`)

1. Frees the NDP array of `AWDL ivars+0x41C8` (`IOFreeTypeVarImpl`, 104 B × counter of
   `+0x41D8`).
2. **`release` of the 13 AWDL timers** (`AWDL ivars+0x2848`…`+0x28A8`), without prior `cancelTimeout` or
   `disable`. Handlers (from `initWithInterface`): `handleMasterIndicationTimer` (+0x2848),
   `handleUmiTimer` (+0x2850), `handleMulticastTimer` (+0x2858), `handleAssistedDiscoveryTimedOut`
   (+0x2860 `?`, by creation order), `handleSuspendTimer` (+0x2868),
   `handleAWDLRecoveryTimer` (+0x2870), `jumpStartDFSProxy` (+0x2878), `discoveryTimeout`
   (+0x2880), `handleLinkChangeTimer` (+0x2888), `handleInfraMonitorTimer` (+0x2890),
   `handleAwdlScanTimer` (+0x2898), **`handleAwdlLLATimer` (+0x28A0)**, and `actionFrameProcessThread`
   at +0x28A8 `?` (by elimination).
3. Tail to `IO80211PeerManager::freeResources` (`0xffffff8002249000`).

### 6.5 `IO80211PeerManager::freeResources` (`0xffffff8002249000`)

1. **`disableWorkQueueSources`** (slot +0x258, `call` at `0xffffff800224900c`), which on the
   three timers of the base does `cancelTimeout` (slot +0x218) and `disable` (slot +0x158):
   `+0x720` data-path (`0xffffff800224983d`/`0xffffff8002249851`), `+0x708` unflow-control
   (`0xffffff800224986a`/`0xffffff800224987e`), `+0x718` interface monitor
   (`0xffffff8002249897`/`0xffffff80022498ab`); then `IO80211CommandQueue::flush` of queue
   `+0x768` (`call` at `0xffffff80022498cb`), with `dword ivars+0x6B0 = 0` and byte
   `ivars+0x714 = 0` (pseudocode).
2. `IO80211Peer::stop` (slot +0x158, `call` at `0xffffff8002249025` and `0xffffff800224905f`) +
   `release` (`0xffffff800224903e`, `0xffffff8002249078`) of the pseudo-peers `+0x530`/`+0x538`.
3. `destroyReporters` (slot +0x190, `call` at `0xffffff8002249090`).
4. Fault reporter (`ivars base+0x8C8`): `IO80211FaultReporter::unregisterCallbacks(manager)`
   (slot +0x128, `call` at `0xffffff80022490ac`; `0xffffff80022c0ba0`) + `release`
   (`0xffffff80022490c5`); and `release` of the object of `+0x8B0` (`0xffffff80022490e6`).
5. `destroyPeerManagerDatapathPcapLogger` (`call` at `0xffffff80022490fb`).
6. `release` of the CC loggers (`ivars+0x9D8`, `+0x9E8`: `0xffffff8002249113`,
   `0xffffff8002249134`) and close+release of those of `+0x9D0` and `+0x9E0` (slot +0x870 at
   `0xffffff8002249155` and `0xffffff800224918f`, then `release`).

### 6.6 `IO80211AWDLPeerManager::free` (`0xffffff80021c4aae`) — chain of the final release

1. `destroyReporters` (slot +0x190) and byte `ivars base+0x8D8 = 0`.
2. `release` of: 3 `IOPacketQueue` of `AWDL ivars+0x28B8`–`+0x28C8`, 2 of `+0x28D0`/`+0x28D8`,
   the `IO80211P2PSupervisor` of `+0x1E0` (the global singleton `_supervisor` survives; only this
   reference falls), 2 service queues `+0xA5C0`/`+0xA5C8`, the
   `IO80211PeerBssSteeringManager` of `+0x40E8` and the `IO80211RealTimePeerManager` of `+0x1E8`.
3. `IOFreeTypeImpl` of the block of `+0xFE90` (`call` at `0xffffff80021c4c08`).
4. `resetServiceRegStats` (`call` at `0xffffff80021c4c1b`) and
   `freeAwdlPacketDescriptorPool` (`call` at `0xffffff80021c4c23`).
5. `memset_s` of the 0x10440 B of AWDL ivars (`call` at `0xffffff80021c4c38`) and
   `IOFreeTypeImpl` (`0xffffff80021c4c50`).
6. Tail to `IO80211PeerManager::free` (`0xffffff80022492f6`).

### 6.7 `IO80211PeerManager::free` (`0xffffff80022492f6`)

On the base ivars block (0xC48 B): `IOLockFree` of `+0x740` (`call` at
`0xffffff800224931c`) and `+0x748` (`0xffffff800224933c`); `release` of the data-path lock `+0x78`
(`0xffffff800224935c`); `release` of command gate `+0x518` (`0xffffff800224937a`); the 4 queues
of descriptor pools `+0x4F8`–`+0x510` with `IO80211CommandQueue::flush` (`call` at
`0xffffff800224939c`) + `release`; the global log buffers (`IOFreeData` at
`0xffffff80022493e9` and `0xffffff8002249418`); the peer queue `+0x540` (flush
`0xffffff8002249442` + release `0xffffff800224945d`) and queues `+0x760`, `+0x758`, `+0x768`
(flush at `0xffffff800224947e`, `0xffffff80022494ba`, `0xffffff80022494f6` + releases); the
`IO80211P2PDataPathManager` of `+0x00` (`0xffffff8002249531`); the stopwatch `+0x5C0`
(`0xffffff8002249605`); the three timers `+0x718`, `+0x708`, `+0x720` (`release` at
`0xffffff8002249599`, `0xffffff80022495bd`, `0xffffff80022495e1`); `+0x9B0` (`0xffffff8002249629`);
`IOLockFree` of `+0x628` (`0xffffff800224964a`); the controller workqueue `+0x528`
(`release` at `0xffffff8002249665`); `memset_s` (`0xffffff8002249682`), `IOFreeTypeImpl`
(`0xffffff8002249696`) and `IO80211QueueDescriptor::free`.

## 7. Reference counter of the manager

Located retainers:

| Retainer | Acquisition | Release |
|---|---|---|
| `ivars+0x28` of the vif | publication in `IO80211SkywalkInterface::start` | `release` in `swi_stop` (`0xffffff80022f899b`) |
| `IO80211P2PSupervisor` (singleton `_supervisor`, field `+0x10`) | retain in `AWDL::initWithInterface` | `IO80211P2PSupervisor::stop` (`0xffffff8002390a46`), invoked from `awdl_stop` |
| each peer (common ivars `+0x60`) | retain when creating the peer | `IO80211Peer::free` (`0xffffff800223f96a`) |

With the peers already deleted (§6.2) and the supervisor stopped (§6.1), the `release` of `swi_stop`
reaches 0 and the chain `D0 → free` runs synchronously on the thread executing the `stop`.
No other retainer has been located in the KC (the apple80211 bridges use the pointer without
retaining), but the sweep of retainers was not exhaustive `?`.

## 8. Locks and thread context

- The whole teardown (terminate→willTerminate→stop→free, including the destruction of the manager)
  runs on the **IOKit termination thread** that called `detachVirtualInterface`; it does not go through the
  command gate of the controller nor its workqueue.
- The only lock the manager takes is the **data-path lock** (`IO80211RecursiveLock` at
  `ivars base+0x78`), within `removeAllPeers` (`lock` at `0xffffff8002254309`, `unlock` at
  `0xffffff80022543b4`), and it is released before deleting the peers (`removeFlushedPeers` at
  `0xffffff800225441e`). `disableWorkQueueSources` and the whole `free` chain run without it.
- Timer callbacks run on the **controller workqueue** (obtained via slot
  +0xC68). The teardown cancels (`+0x218`) and disables (`+0x158`) the three timers of the base
  before releasing them; the 13 AWDL timers are released without prior cancellation and rely on
  `IO80211TimerSource::free` (`0xffffff80021a71e0`), which calls slot **+0x148** of the workqueue
  (`call` at `0xffffff80021a71f8`) — a cell that in `IO80211WorkQueue` is
  `IOWorkLoop::removeEventSource` (`0xffffff8000ac9660`) — and chains
  `IOTimerEventSource::free` (`tail` at `0xffffff80021a720e`). Whether that detach is a
  sufficient barrier against an in-flight callback is not traced `?`.
- `IOWorkLoop::removeEventSource` has no direct call from IO80211Family: source
  disconnection always goes through the `free` of the objects themselves (the manager never
  calls `removeEventSource` explicitly).

### 8.1 Callbacks to the driver during the teardown

| Callback | Path | Lock | Thread |
|---|---|---|---|
| Slot +0xF38 `freePendingPackets` of the vif | `removeFlushedPeers` → `removePeer` → `flushPacketsWaitingForPPP` (`0xffffff80021bff87`) | none of the manager (data-path already released) | stop thread |
| Slot +0xF38 | `monitorPeer` → `ageoutPacketsWaitingForPPP` (`0xffffff80021aa984`), if a peer expires during the teardown | data-path lock | controller workqueue |
| Slot +0xF30 `sendPendingPacketsToStack` | only via ioctl 415 (`pppDone(true)`); not part of the teardown | controller gate | ioctl thread |
| Slot +0xDF8 `detachPeer` of the vif | `removePeer` (`0xffffff8002217809`); the base implements it in the family, it does not reach the driver without override | none of its own | stop thread |
| Event 47 (`postMessageSync`) | `IO80211PeerManager::removePeer` (`0xffffff8002254248`) | none of its own | stop thread |
| `IOService::close` of the controller (slot +0x5D8 of the provider) | `VI::terminate` (`0xffffff8002330c7e`) | none | stop thread; only if the RTL controller overrides `close` |
| `disableVirtualInterface` (slot +0xD10) | `VI::stop` (`0xffffff8002330d5b`), only if the vif was enabled | none | base: `return 0`, does not reach the driver |

## 9. Gaps that remain in the family

1. **The seat `*(ctrl ivars)+0x190` (the role 6 vif) is not cleaned.** The only located writer
   is the indexed store of `createPeerManager` (`mov ecx, 190h` at
   `0xffffff8002330974`, `mov [rax+rcx], rbx` at `0xffffff80023309df`); a sweep of
   direct stores with displacement 0x190 over the `__text` of IO80211Family gives no result,
   and neither `IO80211Controller::stop` (`0xffffff8002292d08`) nor `disableVirtualInterface` touch it.
   An indexed store to zero elsewhere has not been ruled out `?`. Meanwhile,
   `getInterfaceActiveBitmap` (`0xffffff800229a8c6`) would keep reading the pointer after the
   teardown `?`.
2. The nulling of the registration of the global singleton `IO80211P2PSupervisor` between interfaces is not
   traced (its `initWithInterface` reconfigures the manager on reuse) `?`.
3. The exact serialization between an in-flight `ageoutPacketsWaitingForPPP` (workqueue, with the
   data-path lock) and the `removeFlushedPeers` of the stop thread (without the lock) over the same peer is not
   traced: the family relies on the `+1758` flag of the peer and on queue `+0x758` `?`.
4. `IO80211Peer::flushQueues(0)` on the pseudo-peers (§6.2 step 3) may drop packets from their
   queues; their destination (pool or slot of the vif) is not traced `?`.

## 10. Recipe for the teardown of `awdl0` on the RTL8814

1. **Single entry**: `controller->detachVirtualInterface(vif, true)` (slot **+0xDE8** of the
   controller; equivalent to `vif->terminate(1)`). With the synchronous return, when the call comes back the
   vif has already gone through `terminate → willTerminate → stop → free` and the manager is destroyed. There is no
   need to call anything else of the family for the teardown.
2. **Do not release the manager**: the release is done by `swi_stop` (`0xffffff80022f899b`). If the kext
   captured the pointer (step 5 of the plan), it must drop its own reference (`release`) or the
   counter will never reach 0: leak of the 66 KiB block of AWDL ivars, of the 13 AWDL timers and
   the 3 of the base, and of all the queues.
3. **Do not read `ivars+0x28` after starting the teardown**: it is set to 0 at `0xffffff80022f89a5`;
   a pointer captured before and used after the `stop` is use-after-free (the destruction is
   synchronous with the `stop`).
4. **The overrides +0xF30/+0xF38 of the vif must remain valid until
   `detachVirtualInterface` returns**: during the `stop` the family calls `+0xF38` for each peer
   with held packets (§8.1). With the project's own RX queue, return the packets to the pool at `+0xF38`
   and do not touch the pool afterwards. The packets delivered by `+0xF30` only arrive via ioctl 415,
   which will no longer happen once the daemon stops seeing the interface.
5. **The radio can be off earlier, but not the code**: the `monitorPeer` (workqueue)
   and `removePeer` (stop thread) paths can still invoke slots of the vif while the
   termination runs; the vtable of the vif must remain intact until the `free`.
6. **Do not reuse the seat `*(ctrl ivars)+0x190`** for a second vif without checking that the
   family left it empty (§9.1): the current evidence says it remains uncleaned `?`.
7. A `close` override in the RTL controller (slot +0x5D8 of the provider) would receive
   `(this, vif, options)` from `VI::terminate`; in the base no driver method is called at any point of the
   teardown.
8. The last event toward the daemon is the 47 (`postPeerAbsence`) of `removePeer`, posted
   while the vif still exists; afterwards the event pipe is lost with the vif.

## 11. Cross-build compatibility

All the cited ivar and slot offsets are from 25.6.0 (25G241). The key symbols are
exported on 23J631 and 24H232 (dumps `Docs/re/io80211_{23J631,24H232}/syms.txt`:
`IO80211PeerManager::stop`, `free`, `removeAllPeers`, `freeResources`,
`IO80211AWDLPeerManager::{stop,free,...}`), but the vtable slots and the ivar offsets
(+0x28, +0x58, +0x120, +0x190, +0x530/0x538, +0x540, +0x718…) must be re-verified per build
before fixing constants, same as in [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md) §9 `?`.

## 12. Method and limits

- IDA 9.4 headless (`idat -A -S`) over a copy of `~/re_cache/ida/boot_awdl.kc.i64` in
  `/tmp/awdl_teardown`; decompilation of ~40 functions (dec/…dec5/) and disassembly dumps
  with instruction addresses for the cited sites (s1–s11).
- Vtables resolved cell by cell: `IO80211PeerManager` vptr `0xffffff800245d080` and
  `IO80211AWDLPeerManager` vptr `0xffffff800245a868` (deduced and verified by the data xrefs of
  `stop`/`removePeer`/`removeAllPeers`/`freeResources`/`destroyReporters`, matching
  in the four displacements); `IO80211SkywalkInterface` vptr `0xffffff800246af10` (by the
  cell of `free` `0xffffff800246afa0`).
- The addresses of the factory and of the publication (`0xffffff80022f5bd8`, `0xffffff80023308c2`)
  are taken from [AWDL_PEERMANAGER_CREATION.md](AWDL_PEERMANAGER_CREATION.md) without re-verifying here
  (the store of the controller seat was: `0xffffff8002330974`, `0xffffff80023309df`; and
  `IO80211AWDLPeerManager::withInterface` `0xffffff80021c14bc` was resolved again by name);
  those of `flushPacketsWaitingForPPP`, `pppDone`, the 415 and the ageout come from
  [AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) (the flush call and the `pppDone` one in
  `removePeer` were re-verified: `0xffffff80022177e4`, `0xffffff80022177ee`).
- Explicit `?`: exhaustive sweep of the retainers of the manager (§7), barrier of
  `removeEventSource` against in-flight callbacks (§8), cleanup of seat `+0x190` (§9.1),
  life cycle of the `P2PSupervisor` singleton (§9.2), ageout↔removeFlushedPeers serialization
  (§9.3), destination of the packets of `flushQueues` (§9.4).
