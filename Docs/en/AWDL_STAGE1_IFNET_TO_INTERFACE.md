# From a BSD ioctl on `en2` or `awdl0` to `apple80211RouteExternal`: which object is the interface

It closes the open point of `AWDL_STAGE1_DISPATCH_PATH.md` §4: how an
`SIOCSA80211`/`SIOCGA80211` issued over a BSD name reaches the apple80211 dispatch and which object
travels as the interface. Base: kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64). IOSkywalkFamily
is part of the same kernelcache, so the path reads complete. All addresses are VAs.

## 1. From the `ifnet` to the object: IOSkywalkFamily

| VA | Fact |
|---|---|
| `0xffffff8002b002e6` `IOSkywalkNetworkBSDClient::if_ioctl(__ifnet *, unsigned long, void *)` | The `ifnet`'s `if_ioctl` function. It obtains the `IOSkywalkNetworkBSDClient` with `ifnet_softc(ifp)` (`0xffffff8002b002f9`) and jumps (`0xffffff8002b0032c`) to slot `+0x1C8` of the object at `client+0x90`, passing it the object at `client+0x108`, the `ifnet`, the command and the data. |
| `0xffffff8002affce6` `IOSkywalkNetworkBSDClient::ioctlHandler(__ifnet *, unsigned, void *)` | Handler running under that mechanism. It takes the interface from `client+0xE8`, checks its power state (slot `+0x870`) and calls its slot `+0x910` at `0xffffff8002affe97` with `(iface, ifp, cmd, data)`. |
| vtables | `+0x910` is `processBSDCommand` across the hierarchy: `IOSkywalkNetworkInterface` (`0xffffff8002b3d2b8`), `IO80211SkywalkInterface`, `IO80211InfraInterface` / `IO80211InfraProtocol` and `IO80211VirtualInterface`. |

The interface comes out of the `ifnet`'s softc (the BSD client IOSkywalkFamily creates for that interface),
not from a search by name. Each `ifnet` has its client and each client its interface.

## 2. `en2`: infrastructure interface (`RTLAirInterface`)

`RTLAirInterface` inherits from `IO80211InfraProtocol` and does not overwrite `processBSDCommand`. In the vtable of
`IO80211InfraProtocol` (`0xffffff80024656a0`) slot `+0x910` is `IO80211InfraInterface::processBSDCommand`.

| VA | Fact |
|---|---|
| `0xffffff8002360290` `IO80211InfraInterface::processBSDCommand` | Calls the base method at `0xffffff800236038f`. |
| `0xffffff80022f6e98` `IO80211SkywalkInterface::processBSDCommand` | Calls `performGatedCommandIOCTL(this, cmd, data)` at `0xffffff80022f70e8`. |
| `0xffffff80022f779a` `IO80211SkywalkInterface::performGatedCommandIOCTL` | Builds the `apple80211req` and calls `apple80211RouteExternal(this, 0, cmd, &req, 0, flag)` at `0xffffff80022f78ad` (legacy tables). |

Interface receiving the dispatch: `RTLAirInterface`.

## 3. `awdl0`: virtual interface (`RTLAwdInterface`)

`RTLAwdInterface` overwrites neither `processBSDCommand` nor `handleIoctl`, so it uses those of
`IO80211VirtualInterface`. This path does **not** go through `IO80211SkywalkInterface::processBSDCommand` for
the apple80211 requests.

| VA | Fact |
|---|---|
| `0xffffff8002331b36` `IO80211VirtualInterface::processBSDCommand` | Jumps (`0xffffff8002331b47`) to slot `+0xE80` of `this`, which is `handleIoctl`. |
| `0xffffff8002331c88` `IO80211VirtualInterface::handleIoctl(__ifnet *, unsigned long, void *)` | For `0x802069C8`, `0x802869C8` (`SIOCSA80211`), `0xC02069C9` and `0xC02869C9` (`SIOCGA80211`) it obtains the controller with `getController()` (slot `+0xBA8`) and runs `handleIoctlGated` through the controller's slot `+0x850`, with `this` as target. Any other command goes to `IO80211SkywalkInterface::processBSDCommand` (`0xffffff8002331d1b`). |
| `0xffffff8002331d20` `IO80211VirtualInterface::handleIoctlGated` | Returns 22 if `getController()` is null (`0xffffff8002331d47`) and 6 if bit 0 of `*(this+0x128)+0x52` is active. Otherwise it copies the `ifreq` into a stack `apple80211req` and calls `apple80211RouteExternal(this, 0, cmd, &req, 0, flag)` at `0xffffff8002331e56` (legacy tables). On success it returns to the caller fields `+0x14` and `+0x18`. |

Interface receiving the dispatch: `RTLAwdInterface`. This path skips the checks of
`performGatedCommandIOCTL` (`csr_check` and the `com.apple.wlan.authentication` authorization).

## 4. Is `apple80211req.ifname` (`req+0`) read?

Not for choosing the interface. On both paths the `ifreq` is copied into the `apple80211req` and the name
stays at `req+0`. `apple80211RouteExternal` (`0xffffff800236cd2e`) dispatches on its first argument:
`getController()` through slot `+0xBA8` and the tables by code. `?` The handlers have not been reviewed
one by one; if one read `req+0`, it would do so after the dispatch and would not change the interface.

## 5. Summary for stage 1

| Question | Answer | Evidence |
|---|---|---|
| How is it passed from the `ifnet` to the interface? | `ifnet_softc` gives IOSkywalkFamily's BSD client, and its field `+0xE8` is the interface | `0xffffff8002b002f9`, `0xffffff8002affe97` |
| Interface for an ioctl on `en2` | `RTLAirInterface`, via `IO80211InfraInterface::processBSDCommand` and `performGatedCommandIOCTL` | `0xffffff800236038f`, `0xffffff80022f78ad` |
| Interface for an apple80211 ioctl on `awdl0` | `RTLAwdInterface`, via `handleIoctl` and `handleIoctlGated` | `0xffffff8002331b47`, `0xffffff8002331e56` |
| Is the interface looked up by name? | No | §1, §4 |

Implication for stage 1: a 94 issued over `en2` reaches the legacy 94 handler
(`setVirtualInterfaceCreate`, `AWDL_STAGE1_DISPATCH_PATH.md` §1.3) with `RTLAirInterface`, which passes the
`safeMetaCast` to `IO80211InfraProtocol` of the bridge. One issued over `awdl0` arrives with `RTLAwdInterface`,
the cast fails and it never reaches slot `+0x1160`. The apple80211 requests over `awdl0` require the
controller to exist (else 22) and bit 0 of `*(this+0x128)+0x52` to be zero (else 6).
