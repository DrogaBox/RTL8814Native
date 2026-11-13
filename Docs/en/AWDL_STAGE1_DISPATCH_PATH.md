# Dispatch path of apple80211 and creation of the event pipe

Traces, over the kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64), how an `apple80211` request reaches the
handler of code 94 and which object travels as the interface, and locates the kernel side of the monitoring
of events. All addresses are VAs. What the disassembly does not pin down is marked with `?`.

## 1. Path of a request to the handler

### 1.1 Entry via BSD ioctl (`SIOCSA80211`/`SIOCGA80211`)

| Step | VA | Detail |
|---|---|---|
| Entry | `0xffffff80022f6e98` `IO80211SkywalkInterface::processBSDCommand(__ifnet *, unsigned, void *)` | The second argument (`__ifnet *`) only appears in the signature: the body does not read it and dispatches over `this`. `?` How the glue obtains the `this` pointer from the `ifnet` was not closed here. |
| Gating | — | The order runs on the work loop of the interface; the body calls `performGatedCommandIOCTL` at `0xffffff80022f70e8`. |
| Validation | `0xffffff80022f779a` `IO80211SkywalkInterface::performGatedCommandIOCTL(this, cmd, data)` | Returns 22 if `*(*(this+0x120)+0x30) == 0`; runs `csr_check(4)` if the glue asks for it and `isCallingProcessEntitled("com.apple.wlan.authentication")`. |
| Assembly | `0xffffff80022f779a` | Copies the `ifreq` into a stack `apple80211req` (`v14..v18`) and calls `apple80211RouteExternal`. |
| Dispatch | `0xffffff80022f78ad` | `apple80211RouteExternal(this, 0, cmd, &req, 0, flag)`: `a5 = 0`, i.e. **legacy** tables. |

### 1.2 Entry via `IO80211APIUserClient`

| Step | VA | Detail |
|---|---|---|
| External method | `0xffffff80023526ae` `IO80211APIUserClient::_runDriverCommandHelper(IOExternalMethodArguments *)` | Normal path; the large-buffer one is `0xffffff80023537d8` `_runLargeBufferCommandHelper`. |
| Interface | `0xffffff8002352bfd` | `performGatedCommandIOUC(*(apiUC+0xD8+8), cmd, data, apiUC)`: the interface comes out of the state of the **user client** (the one that opened the client), not from a lookup. On the large-buffer path, `0xffffff8002353fb5`. |
| Thunk | `0xffffff80022f6e26` `IO80211SkywalkInterface::processBSDCommandGated(this, cmd, data, apiUC)` | Only forwards to `performGatedCommandIOUC`. |
| Validation | `0xffffff80022f6e30` `performGatedCommandIOUC` | Same checks as the ioctl route and `apple80211RouteExternal(this, apiUC, cmd, req, 1, 0)` at `0xffffff80022f6e8d`: `a5 = 1`, **modern** tables. |

### 1.3 `apple80211RouteExternal`

`0xffffff800236cd2e` `apple80211RouteExternal(IO80211SkywalkInterface *iface, IO80211APIUserClient *uc, unsigned code, apple80211req *req, bool modern, bool flag)`:

| VA | Done |
|---|---|
| `0xffffff800236cd2e`+ | `ctrl = (*(*iface + 0xBA8))(iface)` (`getController()`); if null, returns 102. |
| — | If `*(uint32_t *)(req + 0x10) == -1` (`APPLE80211_IOC_CARD_SPECIFIC`), it calls `(*(*ctrl + 0xC98))(ctrl, iface, code, req, modern)` and does not touch the tables. |
| `0xffffff800236ce27` | **Modern** SET table: `gSetHandlerTable` (`0xffffff800245fef0`), 8-byte entries, index `code - 1`, valid range `[1, 586]` (out of range: `IOLog` and 102). The GET one is `gGetHandlerTable`, with the same range. |
| `0xffffff800236ce2e` | Entry load: `mov rax, [gSetHandlerTable + rax*8]`. |
| — | **Legacy** tables (`a5 == 0`): `gSetHandlerTableLegacy` (`0xffffff8002479060`, range `[1, 457]`) and `gGetHandlerTableLegacy` (`0xffffff8002470d50`, range `[1, 581]`). |
| — | Call signature. Modern, on SET and GET: `handler(ctrl, iface, apiUC, req)`. Legacy: `handler(ctrl, iface, req, flag)`. Null entry: 102. |
| `0xffffff80024601d8` | Entry 93 of `gSetHandlerTable`: `setVIRTUAL_IF_CREATE` (`0xffffff80022886a8`), signature `(IO80211Controller *, IO80211SkywalkInterface *, IO80211APIUserClient *, apple80211req *)`. The request code is 94 and the index is `code - 1`. |
| `0xffffff8002479348` | Entry 93 of `gSetHandlerTableLegacy`: `setVirtualInterfaceCreate` (`0xffffff8002357f33`). Requires `req+0x18 != 0` and `req+0x20 != 0` (else 22), copies `min(len, 32)` bytes with `IO80211Controller::copyIn`, applies the same WCL rule as the modern handler (`IO80211Glue::sendIOUCToWcl(glue, 0x802869C8, 0x5E, …)` and, if there is no WCL or it rejects, `apple80211setVIRTUAL_IF_CREATE(iface)`), and on success returns the 32 bytes with `copyOut`. Entry 93 of `gGetHandlerTableLegacy` is null. |

### 1.4 Which object is the interface

The two paths end in the same bridge, `apple80211setVIRTUAL_IF_CREATE`, with the same
interface as argument. Answered for both paths: **the interface the request was issued over**, i.e. `this`
in `processBSDCommand` (ioctl) and the pointer stored in the user client at `+0xD8+8` (API), not the
primary interface of the controller and not the result of looking up `apple80211req.ifname`. The
controller only enters as first argument (`getController()`, slot `+0xBA8`) and as target of the
`CARD_SPECIFIC` path (slot `+0xC98`).

`?` That the `this` of the ioctl is always the interface owning the `ifnet` that received the order was not
verified instruction by instruction in the glue; the body of `processBSDCommand` does not check it.

## 2. Event monitoring: kernel side

### 2.1 Creation of the pipe

| VA | Done |
|---|---|
| `0xffffff800235256e` `IO80211APIUserClient::extEventMonitorInit` | External start method of the monitoring: registers `enter` and jumps to `IO80211APIUserClient::createEventPipe` (`0xffffff80023525a5`). |
| `0xffffff8002355702` `IO80211APIUserClient::createEventPipe` | Takes the `IO80211CommandGate` of `*(apiUC+0xD8)+0x10` and calls its slot `+0x128`, which is `IO80211CommandGate::runActionBlock`, with the `createEventPipe_block_invoke` block. On success it marks byte `+0x180` of the client state; in any case it marks `+0x171`. |
| `0xffffff80023557e6` (block `0xffffff80023557d0`) | The block takes the interface from `*(apiUC+0xD8)+8` and jumps to its slot `+0xDC0` (440). |
| vtables | `+0xDC0` is `IO80211SkywalkInterface::createEventPipe` (`0xffffff80022f6bf2`) in `IO80211SkywalkInterface` and `IO80211VirtualInterface::createEventPipe` (`0xffffff80023347fc`) in `IO80211VirtualInterface`. The 440 of `Docs/AWDL_STAGE1_IMPL_SPEC.md` §9 is correct. |
| `0xffffff80023347fc` `IO80211VirtualInterface::createEventPipe(apiUC)` | **It already covers role 6**: if `*(*(this+0x120)+0x58) == 6` it requires the entitlement `com.apple.wifi_awdl.event_monitor`. Roles 8 and 9 require `com.apple.wifi_nan.event_monitor`. If the entitlement is missing, it logs "user client from %s not authorized to monitor events on %s" and returns `0xE00002E2`; otherwise it jumps to the base implementation (`0xffffff8002334863`). |
| `0xffffff80022f6bf2` `IO80211SkywalkInterface::createEventPipe(apiUC)` | On the channel container `*(*(this+0x120)+0xA8)`: reads the count via its method at byte `+0x130` and requires less than `0x18` (else `0xE0000001`); creates `IO80211IOUCEventPipe::withClient` (`0xffffff80022a71d8`) and adds it via the method at byte `+0x1B8` of the container (allocation failure: `0xE00002BC`). That `+0x1B8` is a byte displacement of another class and has no relation to slot 440 of the interface. |
| `0xffffff80023525e6` `IO80211APIUserClient::extQueryUseIOUCEventPipe` | Reads the boot-arg `wlan.useIOUCEventPipe` and returns whether the IOUC pipe must be used. |

### 2.2 What the driver needs

Nothing specific. `RTLAwdInterface` does not override slot `+0xDC0`, so it inherits
`IO80211VirtualInterface::createEventPipe`, and the call is virtual over the interface that opened the
user client, without depending on the concrete class. The creation of the pipe, the limit of 24 and the
entitlement check are solved by the family. The only requirements are that the process
opening the user client has `com.apple.wifi_awdl.event_monitor`, that the role at `*(this+0x120)+0x58`
is 6, and that `awdl0` exists and is published before `wifip2pd` opens its client.

## 3. Consequences for stage 1

- The handler of the 94 receives the interface from the **ioctl or the user client**, so the `safeMetaCast` to
  `IO80211InfraProtocol` can only succeed if the request is issued over an interface whose
  object is `RTLAirInterface` (role 1). A request addressed to `awdl0` would never reach that bridge.
- The ioctl route uses legacy tables and the user client one modern tables. The 94 has a handler in
  both (`setVirtualInterfaceCreate` and `setVIRTUAL_IF_CREATE`) and both end in the same bridge,
  so the `setVIRTUAL_IF_CREATE` override in the infra interface covers both paths. The legacy
  route accepts lengths under 32 (copies `min(len, 32)`); the modern one requires 32.
- The event pipe needs no new code: it suffices that `awdl0` is created and with the
  entitlement of the process. The authorization failure shows as `0xE00002E2` in the family log.

## 4. Open points

- Resolution of `this` from the `ifnet` in `processBSDCommand` (§1.1).
