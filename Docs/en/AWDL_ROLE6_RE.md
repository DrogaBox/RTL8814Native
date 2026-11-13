# AWDL role 6: what AppleBCMWLANCore creates and what IO80211Family requires

## Scope and sources

Static analysis of the IDBs of BootKernelExtensions.kc and of `wifip2pd` included in `Build/ida_awdl/`, plus symbols/vtables extracted for macOS 25.6.0 in `Docs/re/io80211_25.6.0/`. Addresses below are virtual addresses of the indicated kernel/image, not file offsets. The prior hardware log and the state map are in `Docs/ARCHITECTURE.md` and `Docs/ROADMAP.md`. "Observed" means the condition/call appears in pseudocode or symbols; it does not substitute a hardware test of the RTL8814 layer.

## 1. Call path and roles

1. `IO80211Family` registers the static function `setVIRTUAL_IF_CREATE` on `IO80211Controller` (`Build/ida_awdl/flow.log`, `0xffffff80022886a8`). It requires request length 32 (`a4+24 == 32`) and a non-null data pointer. If the WCL bridge does not intercept/fail in the special form `-528340977`, it calls `apple80211setVIRTUAL_IF_CREATE(a2)`. That function is the bridge to the controller's virtual override; there is no rule in the pseudocode filtering role 6.
2. The Broadcom override `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`Build/ida_awdl/core.log`, `0xffffff80016788f4`; implementation also in the segment of `Build/ida_awdl/kernel.log`) reads the role as a dword at `request+12`:

   ```text
   role 8..10 -> createNANInterface(request)
   role 6     -> if prox already exists, success; if not, createProxInterface(request)
                on success writes getBSDName() at request+16 (max 16 bytes)
   role 7     -> build AppleBCMWLANIO80211APSTAInterface and start(prefix="ap")
                if the interface already exists, -536870190
   other       -> -536870911
   ```

   The name field starts at `+16`; the same handler zeroes the two qwords there before copying the name. If the AWDL object already exists, it returns its name again. The role 7 handler uses its own state slot, different from the proximity pointer.
3. The generic factory `IO80211Controller::createVirtualInterface` (`Build/ida_awdl/attach2.log`, `0xffffff800229612e`) returns null except `role == 7`; for 7 it allocates `IO80211VirtualInterface`, runs `init(controller, mac, 7, "ap")` and frees on failure. Therefore **role 7 is the generic virtual AP**, not AWDL; **role 6 reaches the driver's `setVIRTUAL_IF_CREATE` override**. It is not inferred that the dispatcher assigns the 6 any other semantics.
4. `IO80211Controller::attachVirtualInterface` (`Build/ida_awdl/kernel.log`, `0xffffff80022960aa`) uses the virtual factory, attaches the object to the controller and returns it to the caller; it is the generic route and not the call observed in `createProxInterface`. AWDL uses the specialized driver path described in §3.

Summarized pseudocode:

```text
Family setter(request[32]):
    if WCL does not consume request:
        apple80211setVIRTUAL_IF_CREATE(controller, request)

AppleBCMWLANCore setter(request):
    switch request.role:
      case 6: create/reuse proximity interface; request.name = BSD name
      case 7: create/start APSTA interface (prefix ap)
      case 8..10: create NAN interface
      default: unsupported role
```

## 2. Hierarchy, vtable and what is "mandatory"

- `AppleBCMWLANProximityInterface` is a concrete class whose immediate parent is **`IO80211AWDLProtocol`**: the `withOptions` constructor (symbol `__ZN30AppleBCMWLANProximityInterface11withOptions...`, `0xffffff80017159e6`) uses its metaclass, builds `IO80211AWDLProtocol` with `AppleBCMWLANProximityInterface::gMetaClass` and sets the vtable `__ZTV30AppleBCMWLANProximityInterface` (`syms_bcm.txt:4086`, vtable at `0xffffff80017f5b68`). Its specialized `init` is invoked through slot `+4552` (`0x11c8`) with arguments `core, MAC, 6, name`, and frees the object on failure (`Build/ida_awdl/flow.log`, 0xffffff80017159e6).
- The vtable of `IO80211InfraProtocol` shows in slots 413/414 the methods `IO80211SkywalkInterface::init(IOService*, ether_addr*)` and `isInterfaceEnabled`, and in 461 `IO80211InfraInterface::setLinkState`; the AWDL child replaces at least part of the protocol interface. The vtable of `AppleBCMWLANSkywalkInterface` identifies the generic `start(IOService*)`/`stop(IOService*)` in slots 184/185, and the specialized Skywalk start in slot 668 (`vt_AppleBCMWLANSkywalkInterface.txt:185-186, 668-669`).
- The symbol list of the class (Docs/re/io80211_25.6.0/syms_bcm.txt:1302-1445) demonstrates implementations of `init`, `start`, `stop`, `free`, `enable/disable`, `enableDatapath/disableDatapath`, queues/pools (`getTxSubQueue`, `getTxPacketPool`, `getTxCompQueue`, `getRxCompQueue`), and AWDL handlers (`set/getAWDL_SYNC_*`, channel sequence, presence, election, encryption, etc.). It is a sample of the class's overrides/functions; asserting exactly all overwritten slots would require numerically comparing each entry of both vtables. **Not verified here**.
- **A minimal list of "mandatory" virtual methods that IO80211Family validates to admit role 6 cannot be derived from the binaries.** The request presents no class/vtable check for AWDL: success comes from the override and from the object registering correctly on Skywalk. What is proven is what Broadcom implements and the generic path of role 7, not a minimal contract of role 6.

## 3. Lifecycle and registration of `awdl0`

1. `AppleBCMWLANCore::createProxInterface` (`Build/ida_awdl/kernel.log`, `0xffffff800165dcbe`) creates the object only if the AWDL slot is empty; if it already exists it does not duplicate. It calls `AppleBCMWLANProximityInterface::withOptions(core, MAC, ..., name)` and saves the reference in the core's private state.
2. `withOptions` does the typed allocation of 320 bytes and runs `init(core, MAC, 6, name)`. That init (`Build/ida_awdl/flow.log`, `0xffffff8001718db0`) calls the class's `init()`, assigns its ivars, calls the Skywalk/infra init, assigns the interface ID (`2` in the usual branch; `0xffffffff` if the observed version/config value is lower than 5), configures the channel callback and allocates `AppleBCMWLANTaskmaster` with work queue. It fails if it cannot create the taskmaster. It configures default times/values and keeps the required management/transmission objects. These are private data, not an ABI recipe to copy.
3. The creator binds the protocol to the core through virtual slot `+1648` and calls `IOSkywalkEthernetInterface::initRegistrationInfo`; it builds a Skywalk registration and calls `AppleBCMWLANProximityInterface::start(interface, core, RegistrationInfo*)` (`0xffffff8001729376`). If the IDB interprets those slots as equivalent names: they are seen in vtable `AppleBCMWLANSkywalkInterface` (`start` slot 668) and the infra route starting from `IO80211SkywalkInterface`.
4. The specialized `start` first starts the base Skywalk layer; it obtains the `AppleBCMWLANBusSkywalk` from the provider and builds/attaches the datapath descriptor and queues to the bus. It registers the TX/RX completion and submission queues enumerated in the descriptor through `IOSkywalkPacketQueue::set...`; finally `IOSkywalkEthernetInterface::registerEthernetInterface(this)`. On error it undoes registrations/enable and returns failure (`Build/ida_awdl/flow.log`, function from `0xffffff8001729376`, especially calls to `registerEthernetInterface`, and `kernel.log` creator from `0xffffff800165dcbe`).
5. On success, `setVIRTUAL_IF_CREATE` queries `IO80211SkywalkInterface::getBSDName` and copies the name to the request's ABI (`core.log`, `0xffffff80016788f4`). The name `awdl0` rests on the class method `getBSDNamePrefix` (`syms_bcm.txt:1309`) and on the result reported in the prior hardware analysis (`Docs/ARCHITECTURE.md`, 2026-10-27 AWDL stage 2). No literal appears in the init pseudocode that proves the trailing `0` by itself; the convention/prefix is class evidence, the final name corroborated by prior state.
6. The controller also saves the interface in `IO80211Controller::setAwdlInterface` (`Build/ida_awdl/kernel.log`, `0xffffff8002299c24`), which writes a pointer into controller state. The family and AWDL peer manager can then associate peers/multicast with that object.
7. On network/IORegistry: the start registers the Ethernet/Skywalk interface, which is the step publishing the netif to the stack. **Neither an exact list of IORegistry properties nor all the BSD attach/naming collars was recovered in the static evidence here**; they are not invented. The registration by itself does not prove the datapath transmits/receives valid frames either.

## 4. `wifip2pd` and the apple80211 requests

**Proven:** `Build/ida_awdl/probe.log` identifies the analyzed binary as `Build/ida_awdl/wifip2pd.x86_64`. Its function `sub_10036C2B0` (pseudocode around `probe.log:49140-49290`) looks up/queries its AWDL interface; if it does not find it, it emits "No AWDL interface found. Creating AWDL interface" and triggers a creation task (`sub_1003654A0`). The hardware observation documented in `Docs/ARCHITECTURE.md` 2026-10-27 confirms the request `APPLE80211_IOC_VIRTUAL_IF_CREATE` (type 94, length 32) with role 6 toward the driver. There is no `Apple80211RawSet` call attributed by xref to that creation task within the retained pseudocode; therefore, the exact Swift → creation ioctl connection stays **unverified in this sample**.

The `wifip2pd` IDB contains strings/symbols of types for AWDL requests, and the Broadcom IDB provides methods for its handlers. This proves those methods exist in the driver, but not who calls them nor that all are part of the first boot. The table avoids turning method names into observed calls:

| Request/capability | Observable ID/handler | Proven state and Broadcom answer |
|---|---|---|
| `VIRTUAL_IF_CREATE` | `APPLE80211_IOC_VIRTUAL_IF_CREATE`, type 94, 32 bytes; request.role `+12`, name `+16` | **Call proven in hardware log**; role 6 creates/reuses and copies the BSD name, success only after completing start/registration (§1, §3). |
| `AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | numeric type not recovered; class has get `0xffffff800172421a` and set `0xffffff8001724436` | **Unverified** that `wifip2pd` sends it when creating awdl0; handler builds channel sequence/commands per symbols `syms_bcm.txt:1402-1405`. Do not assume null success. |
| `AWDL_SYNC_PARAMS`, `AWDL_SYNC_ENABLED`, `AWDL_SYNC_STATE` | numeric types not recovered; get/set symbols `syms_bcm.txt:1343-1345, 1370-1374` | **Unverified** in the creation sequence; Broadcom reads/updates the sync state and sends work to its taskmaster (exact detail of each validation not reviewed). |
| `AWDL_PRESENCE_MODE`, `AWDL_DEVICE_CAPABILITIES`, `AWDL_ELECTION_*`, `AWDL_BSSID`, `AWDL_ENCRYPTION_*` | IDs not recovered; class setters/getters `syms_bcm.txt:1348-1363, 1381-1383, 1395, 1399` | The handlers are present, but **unverified** which requests `wifip2pd` sends after creation nor which are indispensable for the interface to register. |
| Statistics, peer cache, low-latency / traffic registration | Symbols `getAWDL_DEV_STATS`, `getAWDL_CCA`, `getPEER_CACHE_*`, `setAWDL_PEER_TRAFFIC_REGISTRATION`, etc. in `syms_bcm.txt:1346-1347, 1384, 1400-1401` | Presence on Broadcom, no proof of use/order by `wifip2pd`; there is no ID or result attributable with the preserved evidence. |

The `wifip2pd` probe contains an `Apple80211RawSet` in pseudocode `sub_10004B300` (`probe.log` ~41452), with `type = 293`, `len = 24`, and it handles the error; the same body then tests `if_nametoindex` of hardcoded names after the setter. Without symbols/safe decoding of the key path and without an xref to the main AWDL branch, **it is not attributed to AWDL nor labeled as an apple80211 ID**. AWDL request IDs apart from type 94 remain unverified.

Consequently, it is not possible to answer rigorously the request "complete list of IDs wifip2pd uses afterward and minimal Broadcom answer per one" with the saved pseudocode: the Broadcom IDB offers handlers but not the client sequence; the IDA extract of `wifip2pd` does not expose the flow of all the RawGet/RawSet. That list stays as **pending**, not completed through inference.

## 5. Conclusion: can RTL8814AirLayer do it without Broadcom?

- A local subclass of `IO80211ControllerV2` can receive the request if it implements the `setVIRTUAL_IF_CREATE` override (in our ABI it is declared in `Source/IO80211/IO80211_25_6_0.h`, slot 556). The hardware precedent is that when activating `CARD_CAPABILITIES` byte `+8` bit 0, `wifip2pd` stops rejecting the hardware by capability and sends role 6; with the current stub, the kernel returns `0xe00002c7` (`Docs/ARCHITECTURE.md`, entries 0.27).
- **A superficial `awdl0` is not an accepted/useful AWDL interface.** The Broadcom path depends on the private class `AppleBCMWLANProximityInterface`/`IO80211AWDLProtocol`, state/taskmaster, request handler, registration of multiple Skywalk queues with the Broadcom bus and AWDL frame send/receive. The vtable of that class is not exported as a construction API for an independent kext; the setter of IO80211Family only hands over the request, it does not build those resources for role 6.
- An independent implementation could be explored that registers its own interface compatible with the available public/ABI contracts and controls a datapath, but **it is not demonstrated** that macOS accepts that different class for AWDL/peer manager registration nor that implementing a selection of virtual methods suffices. It is not correct to assert it is absolutely impossible without Broadcom; what holds is that it cannot be done correctly with `setVIRTUAL_IF_CREATE` returning success and an `awdl0` chain alone.
- The pieces that cannot be reconstructed as a portable contract from this binary: full format/ABI of the bus registration parameters, private queues/owners protocol, AWDL peer manager integration and the algorithm/firmware for synchronization and channel sequence. The pseudocode shows the Broadcom route and its calls, not their complete specifications. The RTL8814 hardware would additionally need coexistence/time-slicing and real AWDL action; having a BSD interface does not demonstrate radio support.

**Honest summary:** the IOC can be accepted syntactically with a custom override; there is no proof that a functional role-6 interface can be created using only the exposed generic hierarchy. A datapath/state reimplementation would be substantial research and work, with unverified private integration. Until registration, subsequent requests, Skywalk linkage and AWDL traffic are verified on hardware, Stage 2 stays blocked.

## 6. Sequence of `wifip2pd` requests after creating role 6

### What the preserved evidence allows asserting

The pseudocode saved for `sub_10036C2B0` (`Build/ida_awdl/current_probe.log`, `0x10036c2b0`; prior extract `Build/ida_awdl/probe.log`, approximately lines 49140–49290) only allows reconstructing: look up an existing AWDL interface; if there is none, log "No AWDL interface found. Creating AWDL interface"; call the creation route (`sub_1003654A0`, `0x1003654a0`); and, if an instance is left in state, pass it to the continuation. The preserved Swift initializer (`Build/ida_awdl/awdl_initializer_excerpt.txt`, `sub_100416D00` at `0x100416d00`) creates `AWDLInterface` state and calls `sub_10041B1F0` (`0x10041b1f0`) to create the low latency companion interface. If that call fails, it writes "Failed to create the companion low latency interface for AWDL" and continues with the initialization; **this is an observed Swift call error, not proof of a tolerated RawSet**.

The xref extraction of IDA is documented in [`Docs/AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md): it confirms that `sub_100047660` does `RawGet` type 12/len 21 before `RawSet` type 94/len 32 to create the role 6 interface, and that it waits on a semaphore up to 30 s after success. It also documents the `awdl` name armed for the role 6 handle. The event releasing the semaphore and the subsequent AWDL configuration requests remain unresolved.

### Ordered table and state

| Order | Request / operation | ID, type and size | Data / answer the client expects | Failure and evidence |
|---|---|---|---|---|
| 1 | Query the interface capabilities before creating AWDL | `Apple80211RawGet`, type 12 (`CARD_CAPABILITIES`), len 21 | The query precedes creation in `sub_100047660`; it is repeated in three branches. Source: [`Docs/AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md), § `sub_100047660`. | Result consumed as a capability query; answer/failure of this call: not described in the cited evidence. |
| 2 | Create virtual AWDL interface | `Apple80211RawSet`, type 94 (`VIRTUAL_IF_CREATE`), len 32 | MAC at `+4..+9`; role `6` at `+0x0C`; the role 6 branch arms the name `awdl` for the handle. The buffer includes a `u32` at `+0`, whose meaning is not asserted here. Source: [`Docs/AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md), § `sub_100047660`. | If `RawSet` returns an error, `wifip2pd` logs it and throws an exception, without retrying. On the prior hardware, the stub returned `0xe00002c7` and "Operation Not Supported" was observed (`Docs/ARCHITECTURE.md`, AWDL section, 2026-10-27). |
| 3 | Wait for the signal associated with the successful result of `VIRTUAL_IF_CREATE` | Not an Apple80211 request; semaphore with 30 s timeout | After successful `RawSet`, the daemon waits up to 30 s on a semaphore associated with the role. Source: [`Docs/AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md), § `sub_100047660`. | The event/callback signaling the semaphore and the fields it consumes are still **PENDING verification in IDA**. |
| — (other branch) | RawSet in `sub_10004B300` | Type 293; len 24 | The body then does `if_nametoindex`; it has not been resolved what request it represents. Source: [`Docs/AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md), § `sub_10004B300`. | Observed, but **not attributed to AWDL**. The assignment to an apple80211 ID and any relation with AWDL creation stay pending. |
| 4 | Later requests on the AWDL handle (`get/setAWDL_*`, including the companion low-latency operation) | IDs, lengths and buffers **PENDING verification in IDA**; the type 97 seen in a generic wrapper is not identified | The Broadcom handlers are listed in `Docs/re/io80211_25.6.0/syms_bcm.txt:1343–1410`, but they do not prove which requests `wifip2pd` sends, their order nor the expected data/results. The xref report mentions the companion call can fail and the initializer continues, without identifying the internal ioctl. | **PENDING verification in IDA** through the client's xrefs; do not infer calls from the handler inventory nor extrapolate the error handling of `VIRTUAL_IF_CREATE`. |

### Minimal supported pseudocode

```text
sub_10036C2B0 (0x10036c2b0):
    awdl = find_existing_awdl()
    if awdl == nil:
        log("No AWDL interface found. Creating AWDL interface")
        create_awdl_via_task(sub_1003654A0, 0x1003654a0)
    if the task leaves awdl in state:
        continue_with_awdl(awdl)

sub_100416D00 (0x100416d00):
    initialize AWDL Swift state
    attempt sub_10041B1F0()  // low-latency companion; internal ioctl unresolved
    if there is an error: log it and continue
```

The xref report confirms the role 6 branch arms the handle name `awdl` (`Docs/AWDL_WIFIP2PD_REQUESTS.md`, section `sub_100047660`). It does not prove by itself that each `RawGet/RawSet` uses the final BSD name `awdl0`; that detail stays **PENDING verification in IDA**. After the success of type 94, the proven operation is the semaphore wait of up to 30 s, not an additional identified Apple80211 request.

**Pending verification in IDA:** identify the event and the fields releasing the 30 s semaphore; map types 97 and 293 to their Apple80211 IDs (type 293 is not attributed to AWDL); and trace the subsequent `get/setAWDL_*` requests over the created handle, including order, buffers and error behavior. These points stay open per [`Docs/AWDL_WIFIP2PD_REQUESTS.md`](AWDL_WIFIP2PD_REQUESTS.md), "What is missing". The behavior under `unsupported` is not generalized beyond the observed failure of `VIRTUAL_IF_CREATE`.
