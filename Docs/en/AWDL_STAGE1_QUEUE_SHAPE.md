# Queue shape of `awdl0` (role 6): is 1 TX submission + 1 TX completion + 1 RX completion enough?

It closes the `?` of `AWDL_STAGE1_IMPL_SPEC.md` §5.4: whether the family accepts for a role-6 virtual
interface the shape already working on the STA path (1 TX submission, 1 TX completion, 1 RX completion,
one pool per direction) or whether Broadcom's set is required (4 TX submissions, one per AC, plus TX
completion, RX completion and the optional multicast). Base: kernelcache of macOS 26.7.1 (25G241,
Darwin 25.6.0, x86_64), IDA headless (`idat -A -S`) over a copy of `boot_awdl.kc.i64`. All
addresses are VAs. Every row marked as instruction was read in the disassembly; the ones only
citing a function head rest on the decompiled body. Whatever was not fixed carries `?`.

## 1. Answer

**Yes: 1 TX submission + 1 TX completion + 1 RX completion registers and boots on a role-6
interface.** No point of the chain counts queues by role, subfamily or AC; Broadcom's four TX
queues are its own choice for the WMM model, not a requirement.

The minimal shape passing registration and link validation is **a single TX submission queue**:

| Shape | Registration | `ifnet` registration | Queues the netif creates |
|---|---|---|---|
| 0 queues | fails `0xE00002C2` | — | — |
| 1 TX submission | passes | passes | 1 TX, 0 RX (`?` functional registration without RX) |
| 1 TX submission + 1 RX completion | passes | passes | 1 TX, 1 RX |
| 1 TX submission + 1 RX completion + 1 TX completion | passes | passes | 1 TX, 1 RX |
| 4 TX submissions (AC 0-3) + 1 TX completion + 1 RX completion | passes | passes, with WMM | 4 TX, 1 RX |

The only count check that rejects is the netif's, not the family's: **if the queue set's
information qword carries the WMM bit (`0x8`), the number of TX submission queues must be
exactly 4**; with 1 queue that bit goes to zero and nothing is required. A set with 0 TX
submission queues is rejected with 22 (`EINVAL`) and the message `num_txqs == 0`. The number of RX queues is not
checked: zero is accepted and simply no RX queue is created.

## 2. Who looks at the queues, in order

| VA | Function | What it does with the queues |
|---|---|---|
| `0xffffff80023308aa` | `IO80211VirtualInterface::start` | thunk to the base; touches no queues |
| `0xffffff80022f5792` | `IO80211SkywalkInterface::start` | creates pipes, peer manager, PM and the flow queue base; no datapath queues, no counts |
| `0xffffff8002b109f0` | `IOSkywalkEthernetInterface::copyRegistrationInfo` | validates the `RegistrationInfo` (version 1, size ≥ `0x130`), copies `0x130` bytes and publishes `IOMACAddress`; it sees no queues |
| `0xffffff8002b10a84` | `IOSkywalkEthernetInterface::registerEthernetInterface` (array) | copies the `RegInfo` and delegates the queue array without inspecting it |
| `0xffffff8002b0a0d6` | `IOSkywalkNetworkInterface::registerNetworkInterface` (array) | decides the information byte (`3` or `11`) with `isWMMSchedModelCapable`, creates the set with `createWithQueues` and the logical link with `createWithQueueSet` |
| `0xffffff8002b13384` | `IOSkywalkQueueSet::createWithQueues` | instantiates the `IOSkywalkQueueSet` (0x80 bytes) and calls `initWithQueues`; on failure it frees and returns null |
| `0xffffff8002b13406` | `IOSkywalkQueueSet::initWithQueues` | **the family's only count and type validation**; fills the information qword's counts |
| `0xffffff8002b0a204` | `IOSkywalkNetworkInterface::registerNetworkInterface` (link) | validates the `RegInfo` (version 1, size ≥ `0x108`, `+0x14` ≤ `0x400`), copies `0x108` bytes, requires a single set with bit 0 of the qword |
| `0xffffff8002b0a7de` | `IOSkywalkNetworkInterface::addLogicalLink` | requires a single set with bit 0 and that the interface has no address yet; it looks at no counts |
| `0xffffff8002b0c406` | `IOSkywalkLogicalLink::initWithQueueSet` | accepts **1 to 16** queue sets; copies each one's information qword |
| `0xffffff8002b00ba6` | `IOSkywalkNetworkBSDClient::message` | on message `0xE0060105` it adds the link to the nexus (`kern_nexus_netif_llink_add`) |
| `0xffffff80009e6360` | `kern_nexus_netif_llink_add` | validates the link configuration (`nx_netif_validate_llink_config`) |
| `0xffffff80009fad40` | `nx_netif_validate_llink_config` | **rejects 0 TX submission queues** and requires 4 if the WMM bit is set |
| `0xffffff80009f9e00` | constructor of the netif's queue set node | copies the counts: node `+0x44` = byte 4 of the qword, node `+0x45` = byte 5 |
| `0xffffff80009f9960` | `nx_netif_llink_ext_init_queues` (name in its log string) | creates `[node+0x44]` RX queues and `[node+0x45]` TX queues, both with indices `0..n-1` |
| `0xffffff8002aff1e4` | `nxp_qset_init` | calls `IOSkywalkQueueSet::initialize` |
| `0xffffff8002aff2c5` | `nxp_queue_init` | bounds the index against the TX or RX count and adds the queue to the provider |
| `0xffffff8002aff562` | `IOSkywalkNetworkBSDClient::start` | checks pools, allocates the nexus provider and creates the `ifnet` |

No `createDatapath` symbol nor equivalent exists in any family class: queue creation belongs to the driver and registration is `registerEthernetInterface`/`registerNetworkInterface`. The name
search with `Datapath` in `IO80211*` and `IOSkywalk*` only returns `printDataPath`,
`reportDatapathIssue` and Broadcom classes' `enableDatapath`/`disableDatapath` methods.

## 3. `initWithQueues`: 1 to 256 queues and four types

The input is validated in three instructions:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff8002b1341a`–`0xffffff8002b1342a` | `lea rdi,[rdx-101h]` / `cmp rdi,0FFFFFFFFFFFFFF00h` / `jb` | the count must be in **1..256**; out of range returns 0 |
| `0xffffff8002b13430`–`0xffffff8002b1343c` | `test rsi,rsi` / `test rcx,rcx` | queue array and info non-null |
| `0xffffff8002b13471` | `jz` after `mov rdi,[r12+r15*8]` | any null array entry aborts |

Each queue is classified with `performCommand(0x10000)`, which returns a constant type per subclass:

| Type | Class | Instruction writing the type | Count | Array |
|---|---|---|---|---|
| 1 | `IOSkywalkTxSubmissionQueue` | `0xffffff8002b0d8f0` (`mov dword ptr [rdx],1`) | `+0x60` | `+0x20` |
| 2 | `IOSkywalkTxCompletionQueue` | `0xffffff8002b12e08` | `+0x68` | `+0x28` |
| 3 | `IOSkywalkRxSubmissionQueue` | `0xffffff8002b0e802` | `+0x70` | `+0x30` |
| 4 | `IOSkywalkRxCompletionQueue` | `0xffffff8002b0f578` | `+0x78` | `+0x38` |

A type outside 1..4 is ignored in the counts and arrays, but the queue stays in the flat list
(`+0x18`) and receives `setQueueSet`.

Two counts are forced before allocating the arrays:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff8002b134d5`–`0xffffff8002b134e0` | `cmp rax,4` / `jz` / `mov qword ptr [rbx+60h],1` | TX submission: 4 is kept; any other non-zero value becomes 1 |
| `0xffffff8002b134ed`–`0xffffff8002b134f4` | `cmp qword ptr [rbx+68h],0` / `jz` / `mov qword ptr [rbx+68h],1` | TX completion: any non-zero value becomes 1 |

RX submission and RX completion are not forced. Consequence: the netif never sees a TX count other
than 1 or 4. The excess of TX queues over the forced count is discarded from the array by type (check
`cmp rcx,[rbx+60h]` at `0xffffff8002b13685`) without failing the registration.

At the end the non-empty arrays and the information block are allocated, and the two counts
traveling to the netif are written:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff8002b13702`–`0xffffff8002b13717` | `test byte ptr [r14],8` / `call isWMMSchedModelCapable` / `test al,al` | if the caller set the WMM bit, the shape is revalidated; false aborts |
| `0xffffff8002b13733`–`0xffffff8002b13736` | `mov cl,[rbx+60h]` / `mov [rax+5],cl` | **info+5 = TX submission count** |
| `0xffffff8002b13739`–`0xffffff8002b1373c` | `mov cl,[rbx+78h]` / `mov [rax+4],cl` | **info+4 = RX completion count** |

Any allocation failure returns 0; the caller turns it into `0xE00002C2` (§9).

## 4. `isWMMSchedModelCapable`: four TX queues, one per AC

`0xffffff8002b13774` walks the TX submission queue array and, only for type 1 ones
(`0xffffff8002b137d8`), compares `getServiceClass()` with a table of four dwords and counts one
occurrence per AC (`0xffffff8002b137e9`, `0xffffff8002b137fb`). At the end it requires the four
counters to equal exactly 1 (`0xffffff8002b13809`–`0xffffff8002b1381b`).

With a single queue the loop finds one AC and leaves the other three counters at zero, so it returns
false. That false **rejects nothing**: in
`registerNetworkInterface` (`0xffffff8002b0a0d6`) it only decides the information byte, `3` instead of
`11`, and `initWithQueues` only calls `isWMM` again if the byte carries bit `0x8`, which 3
does not. The netif, in `nx_netif_validate_llink_config`, requires 4 TX queues only when that bit is
set.

## 5. What the netif requires

The link validation (`0xffffff80009fad40`) reads the `llink_init`'s set array and, for each
8-byte entry, interprets byte 5 as `num_txqs`:

| Check | Instruction / string | Result |
|---|---|---|
| number of sets 0 | `*(u8*)(llink+4)==0` → `num qsets is zero` | 22 |
| null set array | `*(u64*)(llink+0x18)==0` → `qsets is NULL` | 22 |
| more than one set with bit 0 | accumulation of `v9 & 1` → `has more than one default qset` | 22 |
| `num_txqs == 0` | entry's byte 5 → `num_txqs == 0` | 22 |
| WMM bit with `num_txqs != 4` | `(v9 & 8) != 0 && v10 != 4` → `invalid wmm mode` | 22 |

There is no RX count check whatsoever. The node constructor (`0xffffff80009f9e00`) copies
`entry[4]` to `node+0x44` and `entry[5]` to `node+0x45`, and `nx_netif_llink_ext_init_queues`
(`0xffffff80009f9960`) creates one netif queue per unit:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff80009f99be` | `call qword ptr [r13+70h]` | `qset_init` with index `[node+0x28]` |
| `0xffffff80009f99d3` | `cmp byte ptr [r15+2Ch],0` | if the RX count (node `+0x44`) is 0, the RX loop is skipped |
| `0xffffff80009f9a0e` | `call qword ptr [r13+80h]` with `r8d=0` | RX `queue_init` for index `r14` (0..n-1) |
| `0xffffff80009f9a34` | `cmp r14,rax` / `jb` | RX loop bound: `[r15+2Ch]` |
| `0xffffff80009f9a39` | `cmp byte ptr [r15+2Dh],0` | if the TX count (node `+0x45`) is 0, the TX loop is skipped |
| `0xffffff80009f9a8d` | `call qword ptr [r13+80h]` with `r8d=1` | TX `queue_init` for index `rbx` (0..n-1) |
| `0xffffff80009f9ab0` | `cmp rbx,rax` / `jb` | TX loop bound: `[r15+2Dh]` |

Message `0xE0060105` of `IOSkywalkNetworkBSDClient::message` (`0xffffff8002b00ba6`, call to
`kern_nexus_netif_llink_add` at `0xffffff8002b00d69`) carries the `llink_init` coming from
`IOSkywalkLogicalLink::getLogicalLinkNexusInfo`; the set array is the one `initWithQueueSet` filled
with each `getQueueSetNexusInfo`'s qword (`0xffffff8002b0c4a3`). That is why the byte
5 that the netif validates is the `info+5` that `initWithQueues` wrote.

## 6. `nxp_qset_init` and `nxp_queue_init`

`nxp_qset_init` (`0xffffff8002aff1e4`) gets the set by index, copies the nexus context and
calls `IOSkywalkQueueSet::initialize` (`0xffffff8002b13a22`). That function requires a non-null work loop
(`0x2b13a22`: if `a2` is null it returns `0xE00002C2`), walks the TX completion queues calling
`performCommand(0x50000)` and hook `+0x1B8`, and then the RX submissions with the same hook. **It requires
no minimum count and does not read the action pointer.**

`nxp_queue_init` (`0xffffff8002aff2c5`) bounds the index against the direction's count:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff8002aff309`–`0xffffff8002aff315` | `cmp [r12+60h],rax` / `ja` | TX: the index must be smaller than the TX submission count (`+0x60`); else 22 |
| `0xffffff8002aff31a`–`0xffffff8002aff324` | `cmp [r12+78h],rax` / `jbe` | RX: the index must be smaller than the RX completion count (`+0x78`); else 22 |
| `0xffffff8002aff32e` | `test r12,r12` | the array's queue must exist |
| `0xffffff8002aff33d`–`0xffffff8002aff345` | call to provider's slot `+0x888` | add the queue to the nexus; error is propagated |

Since the netif asks for indices `0..n-1` and the first index is 0, a count of 0 on TX submission leaves the
netif with no TX queue to create, and the rejection arrives earlier through `num_txqs == 0`. A count of 0 on RX
completion generates no error in validation: the RX loop does not run and no RX queue is created.
`?` Whether the resulting `ifnet` stays usable in that state has not been tested.

## 7. `IOSkywalkNetworkBSDClient::start` and the pools

`0xffffff8002aff562` is the last filter before the `ifnet`. Besides the set with bit 0
(`0xffffff8002aff8c4`, `0xffffff8002aff8c6`) and the WMM bit for the configuration value
(`0xffffff8002aff92b`–`0xffffff8002aff92f`), it checks the interface's two pools:

| VA | Instruction | Effect |
|---|---|---|
| `0xffffff8002aff940`–`0xffffff8002aff947` | `mov rcx,[rdi+98h]` / `mov rax,[rdi+0A0h]` | interface TX and RX pools, written at registration from `registerEthernetInterface`'s arguments |
| `0xffffff8002aff94e`–`0xffffff8002aff970` | `test rcx,rcx` / `test rax,rax` / `xor dl,cl` / `test dl,1` | at least one non-null; if both exist, they must match on bit 0 of `+0xB8` |
| `0xffffff8002aff98c`–`0xffffff8002aff996` | `test rax,rax` / `jz` | both null: jumps to the close |
| `0xffffff8002affb74` | `call IOSkywalkLogicalLink::getLogicalLinkNexusInfo` | the provider's `llink_init` |
| `0xffffff8002affbb5` | `call kern_nexus_controller_alloc_net_provider_instance` | retries 50 times if it returns 16 (`EBUSY`) |

A pool or provider failure does not return a code: it closes the interface and **returns 0**, i.e.
the `ifnet` is not created and `awdl0` does not appear. For T3 that manifests as absence of BSD name, not
as a kext load error.

## 8. `enable`/`disable` with queues without an action

The family does not enable the queues on its own: `IOSkywalkQueueSet::setEnable` (`0xffffff8002b139ce`)
has only two references, both data (vtable cells at `0xffffff8002b40c38` and
`0xffffff8002b43ed8`), none of code. The `ifnet`'s bring-up path
(`IOSkywalkNetworkBSDClient::enableInterface` → `IO80211SkywalkInterface::enable` →
`IOSkywalkEthernetInterface::enable`) touches neither the set nor the queues.

The driver's explicit calls do not read the action either:

| VA | Function | Fact |
|---|---|---|
| `0xffffff8002b0cc76` | `IOSkywalkTxSubmissionQueue::enable` | `IOEventSource::enable` (cell `+0x160` of `__ZTV20IOSkywalkPacketQueue`) and a hook; it reads neither `+0x18`/`+0x20` |
| `0xffffff8002b0efe0` | `IOSkywalkRxCompletionQueue::enable` | same |
| `0xffffff8002b12b2c` | `IOSkywalkTxCompletionQueue::enable` | same |
| `0xffffff8002b0ccd6` | `IOSkywalkTxSubmissionQueue::disable` | cell `+0x168`, clears the `IOEventSource` bit; it does not read the action |
| `0xffffff8002b0f040` | `IOSkywalkRxCompletionQueue::disable` | same |
| `0xffffff8002b12b58` | `IOSkywalkTxCompletionQueue::disable` | same |

The action (`+0x20`) and the target (`+0x18`) are read only when there is pending work and the queue is
enabled: `IOSkywalkTxSubmissionQueue::legacyDequeue` (`0xffffff8002b2c860`) checks bit 0 of
`+0x28` and calls `r12 = [rbx+20h]` at `0xffffff8002b2c8de`; the equivalent RX path does it at
`0xffffff8002b2d657` with `r12 = [rdi+20h]`. With a null action that call jumps to address 0. Stage
1 does not enable the queues, so there is no route to that jump.

## 9. Codes T3 will see

| Value | Name | Origin | Meaning |
|---|---|---|---|
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b109f0` | `RegistrationInfo` with version ≠ 1, size < `0x130` or null pointer |
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b0a204` | on network registration: size < `0x108`, version ≠ 1, null link or `+0x14` > `0x400` |
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b1342a` | 0 queues, more than 256, null array or null info |
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b0a0d6` | `createWithQueues` or `LogicalLink::createWithQueueSet` return null |
| `0xE00002C2` | `kIOReturnBadArgument` | `0x2b13a22` | `IOSkywalkQueueSet::initialize` without work loop |
| `0xE00002BD` | `kIOReturnNoMemory` | `0x2b109f0` | allocation of the `RegistrationInfo` copy |
| `0xE00002BE` | `kIOReturnNoResources` | `0x2b109f0` | failure publishing `IOMACAddress` |
| `0xE00002D8` | `kIOReturnNotReady` | `0x2b2ca7c` | `requestDequeue` without work loop or without netif queue |
| `22` | `EINVAL` | `0x9fad40` | link config: 0 sets, null array, more than one default set, 0 TX queues, or WMM bit with TX count ≠ 4 |
| `22` | `EINVAL` | `0x2aff2c5` | index outside its direction's count, missing queue or failure adding it to the provider |
| no code | — | `0x2aff562` | missing or mismatched pool, or nexus provider failure: it closes and returns 0; there is no `awdl0` |

The netif's rejection by `num_txqs == 0` does not change the registration's return, which had already returned 0.
On the in-kernel registration path the 22 does not even pass through `kern_nexus_netif_llink_add`: it is
produced by `nx_netif_default_llink_config` (`0xffffff80009faf35`, called from
`kern_nexus_controller_alloc_net_provider_instance` at `0xffffff80009a4310`) and consumed by the
alloc itself, which returns it as is; `IOSkywalkNetworkBSDClient::start` aborts with any non-null
value and there is no `ifnet` nor BSD name. The path that does go through `kern_nexus_netif_llink_add` and
`message` (which turns the 22 into `0xE00002BC` at `0xffffff8002b00d6e`) is that of message
`0xE0060105`, whose only chain is DriverKit (`addLogicalLink` ←
`IOUserNetworkWLAN::_AddLogicalLink_Impl`). Closed by `AWDL_STAGE1_BSDSTART_ORDER.md`.

## 10. Application to stage 1

- **Precondition:** the TX submission queue must receive a non-null action. `IOSkywalkTxSubmissionQueue::initWithPool` (`0xffffff8002b0caac`) returns 0 when the action argument (`a8`) is null, and `withPool` then returns null; the STA path passes `txAction` (`RTLAirLayer.cpp:365`). Passing null makes queue creation fail before any check of §5 (`AWDL_STAGE1_SPEC_DELTA.md` §4).
- The plan's three-queue shape (§5.4) passes all checks, with that condition: 1 TX submission leaves
  `info+5 = 1` and the information byte at `3`; the netif adds the link, creates one TX queue and one RX
  queue with index 0, and the `ifnet` is created.
- Adding the 4 queues per AC is not necessary. If wanted in a later stage, they must be exactly
  4 with the `wmmAC` table's service classes for `info` to become `11`; with 4 queues and repeated
  classes it is also accepted (`info = 3`), but the netif creates 4 TX queues all the same.
- The netif does not use the TX completion: neither its count travels in the qword nor does the netif create a queue for it. Its
  presence in the three-queue shape is harmless; its absence would also be accepted.
- Keep §5.4's decision not to enable the queues in stage 1: if the TX
  submission were enabled without an action and the stack emitted a packet on `awdl0`, `legacyDequeue` would call
  address 0.
- If in T3 `awdl0` does not appear with the three-queue shape, the problem is not the count: the shape cannot
  trigger any check of §5. Look in the registration's return, the pools
  (§7) or the `ifnet` registration, not in the queue number.

## 11. Searches executed and pending

| Point | Search | Result |
|---|---|---|
| queue limit | `initWithQueues` guard `0x2b1341a`–`0x2b1342a` | 1..256 |
| types | `performCommand(0x10000)` of the four subclasses | 1, 2, 3, 4 with their instructions |
| counts | coercion `0x2b134d5`–`0x2b134f4` and writing of `info+4`/`info+5` | TX: 4 or 1; TX comp: 1; RX untouched |
| WMM | `isWMMSchedModelCapable` and its call from `initWithQueues` | only changes `info`, rejects nothing |
| netif | `nx_netif_validate_llink_config`, node constructor and queue loop | requires ≥1 TX; 0 RX allowed; WMM asks for 4 |
| role | references to `IO80211SkywalkInterface::getInterfaceRole` (`0x22f5ed8`) | only `AppleBCMWLAN*` code; none in the registration chain nor the family |
| enabling | references to `IOSkywalkQueueSet::setEnable` | two data ones (vtable), none of code |
| `createDatapath` | names with `Datapath` in `IO80211*` and `IOSkywalk*` | does not exist; only `printDataPath`, `reportDatapathIssue` and Broadcom's methods |
| order between `setInterface` and the `ifnet` creation in `bsd_start` | instruction-by-instruction walk | closed by `AWDL_STAGE1_BSDSTART_ORDER.md`: `setInterface` at `0xffffff8002b0a38d` (registration, before `registerService`); `ifnet` at `0xffffff8002affbb5` (BSD client's `start`) |
| `?` | registration with 0 RX queues (loop skipped, no check) | not verified on hardware |
| `?` | concrete `service class` values of the `wmmAC` table | the table was read in the disassembly; its semantics (AC 0-3) has not been mapped to names |
