# Stub values for stage 1: what wifip2pd asks of `awdl0` and what to return

Review, in the order of `AWDL_WIFIP2PD_POST_CREATE.md`, of what the daemon sends after the registration of
`awdl0`, with each request's route and the recommended stub for stage 1. The main finding is
that **the documented post-create sequence contains no request ending in one of the 63
pure slots of `IO80211AWDLProtocol`**: the slots are not on the daemon's critical path while
only the interface is being created.

## 1. The sequence, with its real route

| # | Request | Code | Route | AWDL slot? |
|---|---|---|---|---|
| 1 | `RawGet` `CARD_CAPABILITIES` | 12 | get table; over `en2`, before creating | no |
| 2 | `RawSet` `VIRTUAL_IF_CREATE` | 94 | set table → bridge → slot `+0x1160` of `IO80211InfraProtocol` | no; creates `awdl0` |
| 3 | semaphore wait (not a request) | — | IORegistry notification | — |
| 4 | `RawGet` `VIRTUAL_IF_PARENT` | 97 | get table `getVIRTUAL_IF_PARENT` (`0xffffff800227b7ff`) | no; returns the parent's name |
| 5 | `RawSet` `COMPANION_SKYWALK_IF_CREATE` | 293 | set table `setCOMPANION_SKYWALK_IF_CREATE` (`0xffffff800228b4b8`, `gSetHandlerTable[292]`) | no |
| 6 | ifnet ioctls (`SIOCGIFMEDIA`, `SIOCSIFFLAGS`, `SIOCSIFLLADDR`, `SIOCSIFADDR`, `SIOCADDMULTI`, …) | — | BSD/IOSkywalk path | no |
| 7 | ioctl `'i'` 131 (`0xC1206983`) and ioctl `'i'` 130 (`0xC0806982`, type 7708) | — | BSD socket → `if_ioctl` → `handleIoctl` → `performGatedCommandIOCTL` → `RouteExternal` (id out of range → 102); `AWDL_STAGE1_IOCTL_130_131.md` | no |
| 8 | event monitoring (`Apple80211RawP2PEventMonitoringInit` / `…EventMonitoringInit`) | — | user client + `createEventPipe` | no |

Sources: `Docs/AWDL_WIFIP2PD_POST_CREATE.md` (table of the sequence and payloads) and
`Docs/AWDL_IOCTL_SLOT_MAP.md` (the 63 slots are reached from `apple80211get*/set*` bridges, which the
previous sequence does not invoke). Direct consequence for stage 1: the
`kIOReturnUnsupported` stubs of the 63 slots do not block the creation of `awdl0` nor the exit from the 30 s
wait; what decides is the 94 and, afterward, the 97, the 293, `SIOCGIFMEDIA` and ioctls 130/131.

## 2. What to return at each step

| # | Recommendation | Reason |
|---|---|---|
| 2 (94) | `kIOReturnSuccess` and `payload+0x10` with the name `awdl0` | It is the condition releasing the daemon's wait (`AWDL_WIFIP2PD_DISCOVERY.md`); a failure leaves the daemon without interface and the cycle repeats |
| 4 (97) | primary interface's name (`en2`) as a C-string; `kIOReturnSuccess` | The kernel's handler already does it that way (`getPrimarySkywalkInterface`, `AWDL_STAGE1_OPEN_POINTS.md` §3); the stub is not needed |
| 5 (293) | `102` and not create `llw0` | The companion's absence only produces the log of `AWDL_LLW_COMPANION.md`; the daemon continues with `awdl0` |
| 6 (`SIOCGIFMEDIA`) | `ifm_current = 0x80` | Required by the filter `(ifm_current & 0xE0) == 0x80` (`AWDL_STAGE1_IFM_CURRENT.md` §1); it comes out of the media descriptor, not the `RegistrationInfo` directly |
| 7 (130/131) | nothing: keep the inherited behavior (`RouteExternal` returns 102 for id out of range) | The GET (131, id 0) and the SET (130, type 7708) fall outside `[1, 586]`; the GET fails, the SET is not emitted and the daemon swallows the error through its `throws`; not fatal (`AWDL_STAGE1_IOCTL_130_131.md`) |
| 8 (events) | let the family create the channel | It only requires role 6 and the `com.apple.wifi_awdl.event_monitor` authorization (`AWDL_STAGE1_DISPATCH_PATH.md` §2) |

## 3. The 63 slots: what Proximity does in the vtable tail

Sampling of the last four cells of the concrete vtable `__ZTV30AppleBCMWLANProximityInterface`
(`0xffffff80017f5b68`, vptr `+0x10`), read in this pass:

| Cell | Method | Implementation | Behavior |
|---:|---|---|---|
| `+0x11A0` | `setLLW_PARAMS` | `0xffffff8001726ac8` (2677 B) | configures LLW; forwards to the core's commander/features |
| `+0x11A8` | `setTX_RATE` | `0xffffff8001720c20` (455 B) | fixes the TX rate; forwards |
| `+0x11B0` | `setP2P_COEX_PROTECTION` | `0xffffff800172a968` (820 B) | validates state and forwards to the core |
| `+0x11B8` | `setPSF_DWELL` | `0xffffff800171dfc0` (419 B) | configures the PSF dwell; forwards |

These cells are offsets of `AppleBCMWLANProximityInterface`'s concrete vtable, whose layout does
not match the offsets of the 63 pure slots of `IO80211AWDLProtocol` from `AWDL_IOCTL_SLOT_MAP.md`
(in this vtable, e.g., `+0xFC8` and `+0xFD0` are still `_RESERVEDIO80211VirtualInterface14/15`). What
matters here is the methods' shape, not their numbering. None of the four returns a cached
ivar nor a constant: all four are setters talking to the core (commander/features) and can
fail due to firmware state. `?` The rest of the 63 slots was not sampled in this pass; per the
pattern of the four read and per
`AWDL_PROXIMITY_OVERRIDES.md`, the same shape is expected (setters forwarding, getters reading the
core or an ivar).

Recommended stub for the 63: `kIOReturnSuccess` without touching the payload, unless a hardware
test proves the daemon validates the content. `?` Which of the 63 getters the daemon uses outside the
post-create sequence (e.g. when opening AirDrop) is not in the two consulted lists.

## 4. Fatal and non-fatal

| Request | Fatal? | Evidence |
|---|---|---|
| 94 | yes | without `awdl0` there is nothing wifip2pd can use; the 30 s wait expires (`AWDL_WIFIP2PD_DISCOVERY.md`) |
| 12 | yes | without the AWDL capability the daemon does not enter the role 6 branch (`AWDL_WIFIP2PD_POST_CREATE.md` row 1) |
| 97 | not recorded (`?`) | the daemon saves `parentInterfaceName`; no abort branch documented |
| 293 | no | the absence of `llw0` is logged and the sequence continues (`AWDL_LLW_COMPANION.md`) |
| `SIOCGIFMEDIA` | yes | the media filter decides whether the interface is accepted (`AWDL_STAGE1_IFM_CURRENT.md` §6) |
| 130/131 | no | both return 102 (id out of range); the GET fails, the SET is not emitted, the daemon swallows the `throws` (`AWDL_STAGE1_IOCTL_130_131.md`) |
| events | not recorded (`?`) | `AWDL_WIFIP2PD_CALLERS.md` documents no abort branch |

## 5. Pending

| Point | Search executed | Result |
|---|---|---|
| any of the 63 slots in the post-create sequence? | contrast of the two lists against `AWDL_IOCTL_SLOT_MAP.md` | none |
| behavior of the remaining 59 slots | not executed | `?` |
| fatality of 97, 130/131 and events | reading of `AWDL_WIFIP2PD_CALLERS.md` | no abort branch documented (`?`) |
| slot of the 293 | reading of `gSetHandlerTable[292]` | `setCOMPANION_SKYWALK_IF_CREATE` (`0xffffff800228b4b8`) |
