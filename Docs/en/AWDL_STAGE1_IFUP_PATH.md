# Bringing `awdl0` up and the first transmitted packet: what the kernelcache runs

Trace of what happens in the kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64) when
`ifconfig awdl0 up` sets `IFF_UP` and when the stack sends an IPv6 packet (ND/RS) over the interface, with
the stage 1 code already applied (`Source/AirLayer/AWDLInterface.cpp`, commit `fe29719`). It serves to
predict the outcome of test T3.5 before loading the kext. Base: full kernelcache (IO80211Family
and IOSkywalkFamily are in the same binary), IDA headless (`idat -A -S`) over a copy of
`boot_awdl.kc.i64`. All addresses are VAs; each one cited as a head was checked with
`ida_funcs.get_func(ea).start_ea == ea`. What the disassembly does not pin down goes with `?`.

Later note: `AWDL_STAGE1_QSET_STATE.md` corrects §4 and risk 4 of §7: `netif_qset_enqueue` is not on the path of the first ND/RS (its only caller is flow processing) and the byte `+0x3C` it compares is `nll_state` of the `netif_llink`, which is 1 on a live interface. With the queues enabled, the first packet does reach the no-op action.

## 1. Answer

1. **The ifup succeeds.** `ifconfig awdl0 up` walks `if_ioctl` → `ioctlHandler` →
   `IOSkywalkNetworkBSDClient::enableInterface` → the override `RTLAwdInterface::enable`
   (`AWDLInterface.cpp:294`), which returns `kIOReturnSuccess`, and the `SIOCSIFFLAGS` returns 0 with
   `IFF_UP|IFF_RUNNING` set.
2. **No queue ends up enabled.** `IOSkywalkPacketQueue::initWithPool` writes **0** into
   `+0x28` (`0xffffff8002b08c6d`) after `IOEventSource::init` had set it to 1. That byte
   is `IOEventSource::enabled` (`[rdi+28h]`, `0xffffff8000acaa44`). Stage 1 does not call `enable()` on
   any of the three queues, so all three stay disabled.
3. **The first ND/RS packet does not reach the no-op.** `IOSkywalkTxSubmissionQueue::packetSubmission`
   (`0xffffff8002b2cb2a`) checks `+0x28` before anything else and, with the queue disabled,
   returns `0xE00002D8` (`kIOReturnNotReady`) before touching the ring; the error goes back to the netif and
   `legacyDequeue` (`0xffffff8002b2c860`) does not run.
4. **Correction to the previous documentation.** `AWDL_STAGE1_SPEC_DELTA.md` §4.1 and
   `AWDL_STAGE1_CODE_FIXES.md` §1 state that the queues are born enabled because `IOEventSource::init`
   leaves `enabled = true`. The first half is true and the conclusion is not: `IOSkywalkPacketQueue::initWithPool`
   clears the byte at the end of its initialization. The non-null action remains necessary, but it is not
   sufficient for T3.5 to exercise the TX path.
5. **Minimal fix for T3.5:** have `RTLAwdInterface::enable` enable the queues and `disable` disable them,
   as Broadcom does in `enableDatapath`
   (`0xffffff8001729afc`, described in `AWDL_PROXIMITY_DATAPATH.md` §8). The family base
   `IO80211SkywalkInterface::enableDatapath` (`0xffffff80022f98f4`) only returns 0.

## 2. The ifup chain

`ifconfig awdl0 up` issues `SIOCSIFFLAGS` = `0x80206910` (`-2145359600` signed).

| Step | VA | Done |
|---|---|---|
| `if_ioctl` | `0xffffff8002b002e6` | `ifnet_softc(ifp)` gives the client; it takes `client+0x90` (glue object) and `client+0x108` and jumps to slot `+0x1C8` of the glue with `(glue, client+0x108, ifp, cmd, data, 0)`. At `client+0x108` there is the pointer to `ioctlHandler`, which `start` wrote at `0xffffff8002aff7a1` (`lea rax, ioctlHandler` / `mov [rbx+108h], rax`). `?` The body of the glue invoking that pointer was not walked. |
| `ioctlHandler` | `0xffffff8002affce6` | For `cmd == 0x80206910` it reads `ifnet_flags(ifp)`. If `IFF_UP` (bit 0) is set and `IFF_RUNNING` (bit 6) is not, it runs the ifup. It is also invoked directly with `0x80206910` from `preConnectAction` (`0xffffff8002b008e7`) and `disconnectedAction` (`0xffffff8002b00a70`), i.e. when connecting and disconnecting the nexus. |
| `ifnet_flags` | `0xffffff8000640390` | Returns `*(u16 *)(ifp + 190)`. Bits 0 and 6 are `IFF_UP` and `IFF_RUNNING`. |
| `setRunningState(true)` | `0xffffff80022f85d2` | Writes byte `state+136` and returns 0. The `ioctlHandler` requires 0 to continue. |
| `adjustInterfaceEnable` | `0xffffff8002b006ee` | With `+324` and `+322` set, it compares `+323` (enabled) with `+325` (desired) and calls `enableInterface` or `disableInterface` according to `+325`. |
| `enableInterface` | `0xffffff8002b0079a` | Calls slot `+0x850` of the interface with `(iface, 0)`. If it returns 0, it sets `+323 = 1`, notifies the KDP poller and returns 0. |
| slot `+0x850` | cell of `__ZTV23IO80211VirtualInterface` | In the family it is `IO80211VirtualInterface::enable` (`0xffffff80023308b4`); in `RTLAwdInterface` it is the override of `AWDLInterface.cpp:294`. |
| `RTLAwdInterface::enable` | `AWDLInterface.cpp:294` | Logs "awdl: enable 0x%x" and returns `kIOReturnSuccess` (0). |
| back in `ioctlHandler` | `0xffffff8002affce6` | With 0, it leaves `+324 = 1` and runs `ifnet_set_flags(ifp, flags \| 0x40)`, which sets `IFF_RUNNING`. |
| `processBSDCommand` | `0xffffff8002331b36` | The `ioctlHandler` afterwards calls slot `+0x910` with the same command. It jumps to slot `+0xE80` (`handleIoctl`). |
| `handleIoctl` | `0xffffff8002331c88` | For a non-apple80211 command, it falls to `IO80211SkywalkInterface::processBSDCommand` (`0xffffff80022f6e98`). |
| `processBSDCommand` (family) | `0xffffff80022f6e98` | For a non-apple80211 command it calls the data pointer `0xffffff8002b3fa98`, which holds `IOSkywalkEthernetInterface::processBSDCommand` (`0xffffff8002b1102c`). |
| `ioctl_sifflags` | `0xffffff8002b11090` | Only handles `IFF_PROMISC` (`0x100`) and `IFF_ALLMULTI` (`0x200`); without changes in those bits it returns 0. |

Result: the `ioctl` returns 0 and the interface ends up `UP,RUNNING`. The ifup **neither fails nor hangs**
with the stubs: neither of the two takes locks nor can block; `enable` only writes a log and returns 0.

### 2.1 What the ifup does **not** run

The override of `enable` replaces cell `+0x850`, so none of these layers runs:

| Function | VA | What it would have done |
|---|---|---|
| `IO80211VirtualInterface::enable` | `0xffffff80023308b4` | Call `IO80211SkywalkInterface::enable`. |
| `IO80211SkywalkInterface::enable` | `0xffffff80022f990c` | Call `IOSkywalkEthernetInterface::enable` (`0xffffff8002b114f8`). |
| `IOSkywalkEthernetInterface::enable` | `0xffffff8002b114f8` | Call `IOSkywalkNetworkInterface::enable` (`0xffffff8002b0b41a`). |
| `IOSkywalkNetworkInterface::enable` | `0xffffff8002b0b41a` | Call slot `+0x930` (`setInterfaceEnable(true)`) and write `+216 = (result == 0)`. |
| `IO80211VirtualInterface::setInterfaceEnable` | `0xffffff8002331ba6` | Set `state+113`, create the IOReporters, update the `IO80211MacAddressAgent` and call `setLinkState`. |
| `AppleBCMWLANSkywalkInterface::enableDatapath` | `0xffffff80015d9d7e` | Broadcom model: its own `enable` calls it after the base to enable the data queues. |

Measured consequences:

- `+216` (what `IOSkywalkNetworkInterface::isEnabled`, `0xffffff8002b0b54a`, returns) stays 0. It is
  harmless: that function has no code or data references.
- `state+113` and the `IO80211MacAddressAgent` are not updated. `?` Whether some consumer of the
  family reads `state+113` to filter RX was not checked.
- `reportLinkStatus` (`0xffffff8002b0b49e`) is not called, which is what Broadcom does from its
  `setInterfaceEnable` when the interface is up. `if_link_state` does not change. `?` Whether `wifip2pd`
  requires an active link state to deem `awdl0` operational was not verified.

## 3. Who enables the queues (and who does not)

Byte `+0x28` is `IOEventSource::enabled`. `IOEventSource::enable` (`0xffffff8000acaa40`) sets it to 1
with `mov byte ptr [rdi+28h], 1` (`0xffffff8000acaa44`) and `disable` (`0xffffff8000acaa60`) to 0. The
creation sequence of any of the three queues is:

| Step | VA | Done |
|---|---|---|
| `IOSkywalkTxSubmissionQueue::withPool` | `0xffffff8002b0ca86` | Wraps `initWithPool`. |
| `IOSkywalkTxSubmissionQueue::initWithPool` | `0xffffff8002b0caac` | Requires a non-null action (`a8`), forwards to `IOSkywalkPacketQueue::initWithPool`. |
| `IOSkywalkPacketQueue::initWithPool` | `0xffffff8002b08bc8` | Calls `IOEventSource::init` (leaves `enabled = 1`) and at the end writes **0** into `+0x28` (`0xffffff8002b08c6d`) and 1 into `+0xCD` (`0xffffff8002b08c71`). |

That is: **the queue is born disabled**. Nothing in the family reverts it:

| Candidate | VA | Done |
|---|---|---|
| `IOSkywalkQueueSet::setEnable` | `0xffffff8002b139ce` | Only two references, both of data (vtable cells `0xffffff8002b40c38` and `0xffffff8002b43ed8`); none of code. |
| `IOSkywalkQueueSet::initialize` | `0xffffff8002b13a22` | Walks only the arrays `+0x28` (TX completion) and `+0x30` (RX submission); calls hook `+0x1B8` and `performCommand(0x50000)`. Does not touch the TX submission queues. |
| `nxp_queue_init` | `0xffffff8002aff2c5` | Calls hook `+0x1B8` of each queue of the set with the `netif_queue` and assigns the pointer with `IOSkywalkPacketQueue::setKernNetifQueue` (`0xffffff8002b09016`). |
| `IOSkywalkPacketQueue::initialize` | `0xffffff8002b08cce` | Writes 1 into `+0xCC` (`0xffffff8002b08d85`), the byte that `legacyDequeue` and the completion queue later require. It does **not** touch `+0x28`. |
| `IO80211SkywalkInterface::enableDatapath` | `0xffffff80022f98f4` | Returns 0. It is a base stub. |

Enabling is the driver's responsibility. Broadcom does it in `enableDatapath`
(`0xffffff8001729afc`, `AWDL_PROXIMITY_DATAPATH.md` §8): it enables the TX completion, the RX completion and
each TX submission (slot `+0x150` = `IOSkywalkTxSubmissionQueue::enable`, `0xffffff8002b0cc76`) and asks for
`requestDequeue(0, 0)`. `IOSkywalkTxSubmissionQueue::enable` does exactly what is needed here:
it checks `[+28h] & 1` (`0xffffff8002b0cc84`), calls `IOEventSource::enable` (slot `+0x160`) and, if there is
already a `netif_queue` at `+0x78` and `+0x322` bit 2 is zero, it calls `packetSubmission(false)` and
`sendNotification`.

Stage 1 does none of that: `AWDLInterface.cpp:294-303` only logs and returns 0.

## 4. The chain of the first packet

An ND/RS goes out over `awdl0` when the interface is `UP`. The walk is:

| Step | VA | Done / check |
|---|---|---|
| `ifnet_enqueue_pkt` | `0xffffff80005e65d0` | Passes only if `*(s8 *)(ifp+192) < 0` **and** `(*(u32 *)(ifp+120) & 7) == 2`; else it frees and returns 6 (`ENXIO`). Then it requires `*(u8 *)(ifp+190) & 1` (`IFF_UP`), else it frees and returns 50 (`ENETDOWN`). `?` The meaning of `+192`/`+120` was not identified; `+190` is `if_flags`, confirmed by `ifnet_flags` (`0xffffff8000640390`). |
| `netif_qset_enqueue` | `0xffffff80009fa2a0` | `cmp byte ptr [qset+3Ch], 2` (`0xffffff80009fa2d3`): if the set state is 2, it frees the chain and returns 6 (`ENXIO`). |
| `nxp_queue_tx_push` | `0xffffff8002aff4d1` | It is the `tx_push` of the provider. It calls slot `+0x1D0` of the queue (58) and converts the error with `IOService::errnoFromReturn` (cell `+0x788` of the interface, `0xffffff8000a95ab0`). |
| `IOSkywalkTxSubmissionQueue::packetSubmission` | `0xffffff8002b2cb2a` | **First check: `+0x28 & 1`.** With the queue disabled it returns `0xE00002D8` without consuming a single packet. |
| `IOSkywalkTxSubmissionQueue::legacyDequeue` | `0xffffff8002b2c860` | Only reached with `packetSubmission` already inside. It requires `+0x28 & 1` and `+0xCC & 1`; with `+0xCC` at 0 it calls `purgePackets` (`0xffffff8002b0d870`, slot `+0x2A0`). |
| queue action | `+0x20` of the queue | `RTLAwdInterface::txNoop` (`AWDLInterface.cpp:247`), with `(owner +0x18, queue, array, count, refCon +0x58)`. |
| `IOSkywalkTxCompletionQueue::enqueuePackets` | `0xffffff8002b2dbb6` | Requires `+0x28 & 1`, `+0xCC & 1`, `count < capacity (+0xAC)`, `+0x30 != 0` and `+0x110` bit 0 clear. |
| `IOSkywalkTxCompletionQueue::completePackets` | `0xffffff8002b12b9e` | Per packet, `getSourceQueue` and `notifySrcQueue`; the `IOSkywalkTxSubmissionQueue::packetCompletion` (`0xffffff8002b0d758`) increments the completion counter `+0x134` and signals work. |

### 4.1 What is **not** on this path

- `IO80211VirtualInterface::findOrCreateFlowQueue` (`0xffffff800233270a`) and its variants only have
  references from IO80211/Broadcom handlers (`0xffffff80017f51e0`, `0xffffff80024570b8`,
  `0xffffff800246efe8`, `0xffffff80024737f0`, …); not from the netif submission.
- `IO80211VirtualInterface::dequeueTxPackets` (`0xffffff80023313b2`) has no references: it is the
  API a driver pulling packets on its own would use, not stage 1.
- `IO80211SkywalkInterface::isCommandProhibited` (`0xffffff80022f9bce`) is called from the
  apple80211 command handlers, not from the data path.
- There is no `linkState` check on this route: the only link state looked at is
  `IFF_UP` in `ifnet_enqueue_pkt`.

Therefore, if the queue were enabled, the packet **would** reach `txNoop`; with the current code
it stays in `packetSubmission` with `0xE00002D8`.

## 5. Expected return values

| Call | Success | Failure | Effect of the failure |
|---|---|---|---|
| `enable(unsigned)` (slot `+0x850`) | 0 | anything else | `adjustInterfaceEnable` returns it, `ioctlHandler` undoes `IFF_RUNNING` (`ifnet_set_flags(flags & ~0x40)`), sets `+324 = 0`, calls `setRunningState(false)` and the `SIOCSIFFLAGS` returns the error. |
| `disable(unsigned)` (slot `+0x858`) | anything | — | The return is ignored; `+323` is set to 0 all the same. |
| TX action | number of packets consumed, `0 ≤ n ≤ count` | — | `0`: stall mark at `+0x13C` bit 1 (`+79` dword), the ring does not advance, retried later. `0 < n < count`: partial advance and partial counter. `n > count`: the ring index advances too much (no clipping) and desynchronizes. |
| `packetSubmission` | 0 | `0xE00002D8` `kIOReturnNotReady` | queue disabled. |
| | | `0xE00002D5` `kIOReturnBusy` | `(+0x13C & 5) != 0` (`?` meaning of the two bits). |
| | | `0xE00002DB` `kIOReturnNoSpace` | no free space in the ring; `+0x13C` bit 1 is marked. |
| `TxCompletionQueue::enqueuePackets` | 0 | `0xE00002C2` `kIOReturnBadArgument` | null pointer, `count == 0` or `count ≥` capacity. |
| | | `0xE00002D8` `kIOReturnNotReady` | queue disabled or without `+0x30`. |
| | | `0xE00002D5` `kIOReturnBusy` | `+0x110` bit 0 set. |

## 6. Prediction of T3.5

With the current code (queues created and never enabled):

1. `ifconfig awdl0 up` returns 0; `ifconfig` shows `UP,RUNNING`; the kext log records
   `awdl: enable 0x0`.
2. No panic nor hang: the path takes no own gates nor waits for anyone.
3. `netstat -I awdl0` does not increment the output packets. A `ping6` to a neighbor fails after the
   ND retries; `?` the exact errno the stack sees (the mapping of `0xE00002D8` in `errnoFromReturn`
   was not decompiled).
4. If `txNoop` is instrumented with a log, **it does not appear**: the packet stays in
   `packetSubmission` (0xE00002D8) and the netif drops it.
5. If queue enabling is added, the same `ping6` must produce the call to `txNoop` with
   `count` between 1 and 64, and the completion counter `+0x134` must grow.

Falsification: if with the queues disabled a call to `txNoop` appeared, there would be another path
enabling the queue (for example, a call of `wifip2pd` to `setInterfaceEnable` or `enableDatapath`);
its stack would have to be recorded.

## 7. Risks for T3.5, by probability

| # | Risk | Probability | Evidence | Symptom |
|---|---|---|---|---|
| 1 | The TX submission queue is disabled and the packet does not reach the no-op | **Certain with the current code** | `0xffffff8002b08c6d` writes 0 into `+0x28`; `0xffffff8002b2cb2a` checks it before everything | `ifconfig` OK, zero TX, no panic |
| 2 | The previous documentation takes the queues as enabled and T3.5 is read as a failure of the no-op | High | `AWDL_STAGE1_SPEC_DELTA.md` §4.1, `AWDL_STAGE1_CODE_FIXES.md` §1 | wrong diagnosis; the failure is sought in the action instead of in the enable |
| 3 | Only the TX submission is enabled and not the TX completion: the no-op frees the packets into the pool and the completion counter does not grow | Medium | `0xffffff8002b2dbb6` requires `+0x28`; `packetCompletion` (`0xffffff8002b0d758`) is who increments `+0x134` | T3.5 passes with few packets; stall when exhausting the free space (`?` exact threshold) |
| 4 | `netif_qset_enqueue` drops with `ENXIO` because the set state is 2 | Medium | `0xffffff80009fa2d3`; the writer of byte `+0x3C` was not identified (`?`) | `packetSubmission` is never reached; zero TX |
| 5 | `setInterfaceEnable` does not run: no link report nor `MacAddressAgent` update | Medium-low | `0xffffff8002331ba6` is left out by the override of `enable` | `if_link_state` idle; `?` whether `wifip2pd` requires it |
| 6 | `enable` returns something other than 0 and the `ioctl` undoes `IFF_RUNNING` | Low | `0xffffff8002affce6` reverts if `adjustInterfaceEnable != 0` | `ifconfig up` fails; the interface ends DOWN |
| 7 | The no-op returns `count` but the completion fails: the ring advances and `+0x134` does not | Low (latent) | `0xffffff8002b2c860` advances by the returned value; `0xffffff8002b0d758` is the only one incrementing `+0x134` | stall after exhausting the free space |

## 8. Searches run and open points

| Point | Search | Result |
|---|---|---|
| ifup | `SIOCSIFFLAGS` (`0x80206910`) in `ioctlHandler` | `0xffffff8002affce6`; `IFF_UP`/`IFF_RUNNING` with `ifnet_flags` (`0xffffff8000640390`, u16 at `+190`) |
| driver enable | cells `+0x850`/`+0x858` of `__ZTV23IO80211VirtualInterface` | `0xffffff80023308b4` / `0xffffff80022f9922`; the override of stage 1 replaces the first |
| queue state | writes to `+0x28` | `IOEventSource::init` writes 1 (`0xffffff8000aca922`); `IOSkywalkPacketQueue::initWithPool` writes 0 (`0xffffff8002b08c6d`) |
| enabling in the family | references to `IOSkywalkQueueSet::setEnable` | two of data, none of code |
| initialization in the nexus | `nxp_queue_init` | hook `+0x1B8` (sets `+0xCC = 1`) and `setKernNetifQueue`; does not enable |
| TX path | `packetSubmission` → `legacyDequeue` → action | `0xffffff8002b2cb2a` → `0xffffff8002b2c860` → `+0x20` |
| drop before the provider | `netif_qset_enqueue` | `cmp [qset+3Ch], 2` → `ENXIO` (`0xffffff80009fa2d3`) |
| `?` | writer of byte `+0x3C` of the netif queue set | not identified |
| `?` | mapping of `0xE00002D8` to errno in `errnoFromReturn` | not decompiled |
| `?` | consumer of `state+113` and `if_link_state` in `wifip2pd` | not verified |
| `?` | meaning of the qword `+0x30` of the TX completion queue (`0xffffff8002b2dbb6` requires it non-null) | not identified |
