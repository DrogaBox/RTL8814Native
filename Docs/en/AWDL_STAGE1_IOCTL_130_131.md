# Ioctls `'i'` 130 (`0xC0806982`) and 131 (`0xC1206983`): real path and what the kext needs

Closes the open points of `AWDL_STAGE1_STUB_VALUES.md` §2/§5 about the two ioctls wifip2pd
issues after creating `awdl0`. Base: kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64); annotated
IDB. All addresses are VAs. It corrects the label of row 7 of the post-create sequence table, which attributed them to `IO80211APIUserClient`: they do **not** go over the user client.

## 1. How the daemon emits them (wifip2pd side)

From `AWDL_WIFIP2PD_POST_CREATE.md` §2.5 and `AWDL_WIFIP2PD_CALLERS.md`: it is not the user client. `sub_100058420`
opens an `AF_INET6/SOCK_DGRAM` socket (`sub_1001CFA90(30,2,0)`) and issues two `ioctl(fd, cmd, buf)` BSD:

| Order | Command | Buffer | Content |
|---|---|---|---|
| 1 (GET gate) | `0xC1206983` (`'i'` 131, IOWR 288) | 288 B zeroed | only ifname `awdl0` @0 (`0x100058509`) |
| 2 (SET) | `0xC0806982` (`'i'` 130, IOWR 128) | 128 B zeroed | ifname @0; `u16 @16 = 7708` (`0x1E1C`, `0x1000585c3`); 16 B @24..39 from the `apple80211req` (`{val_len u32, val ptr}`) of the caller (`0x1000585db`) |

The SET only runs if the GET returns 0 and `*(a1+16) != 0` (`0x10005852b`). The Swift method is
`throws`: if the GET fails, there is no SET and the error is swallowed with `errno`. The number `'i'` 130/131 is
built in the daemon, it does not travel to the kernel as such; what crosses is the `apple80211req` of the buffer.

## 2. What serves them in the kernel (over `awdl0`)

A BSD ioctl over `awdl0` enters through `IO80211VirtualInterface::handleIoctl` (`0xffffff8002331c88`). There
only the four canonical `SIOCxA80211` constants (`0x802069C8`,
`0x802869C8`, `0xC02069C9`, `0xC02869C9`) are diverted to `handleIoctlGated`; **any other command falls to**
`IO80211SkywalkInterface::processBSDCommand` (`0xffffff8002331d1b` → `0xffffff80022f6e98`). `0xC0806982`
and `0xC1206983` are none of the four, so they go through that branch.

`IO80211SkywalkInterface::performGatedCommandIOCTL` (`0xffffff80022f779a`) **does not** filter by ioctl
number nor reject the non-canonical ones:

| VA | Done |
|---|---|
| `0xffffff80022f77b8` | If `*(*(this+0x120)+0x30) == 0` it returns 22; else conditional `csr_check(4)` and `isCallingProcessEntitled("com.apple.wlan.authentication")`. |
| `0xffffff80022f779a`+ | Copies the buffer into a stack `apple80211req` (`v14..v18`); the type `u16` of the daemon lands at `req+0x10` (`v16 = a3[2]`). |
| coercion | `v13 = 0xC02869C9`; if `(cmd & 0xFFF7FFFF) == 0x802069C8`, `v13 = 0x802869C8`. The original number is discarded: it **always** calls `apple80211RouteExternal(this, 0, v13, &req, 0, flag)`. |
| dispatch | `apple80211RouteExternal` (`0xffffff800236cd2e`) indexes the table by the internal id `req+0x10`, validated `[1, 586]` modern / `[1, 457]` legacy (`AWDL_STAGE1_DISPATCH_PATH.md` §1.3). |

## 3. Result

- **131 (GET)**: the buffer sets no type, so the internal id is 0. `0 < 1` → out of range →
  `IOLog("IOCTL req out of bounds")` and returns **102**.
- **130 (SET)**: the internal id is **7708**. `7708 > 586` → out of range in both branches → **102**.
- Neither indexes the handler table, so **no pure slot of
  `IO80211AWDLProtocol` is reached** (`AWDL_IOCTL_SLOT_MAP.md`). The 7708 corresponds to no apple80211 method.

The GET (131) already returns non-zero, so the SET (130) is not even issued; the daemon swallows the
error via its `throws`. It matches `AWDL_WIFIP2PD_CALLERS.md` not documenting an abort branch: the
131/130 pair **is not fatal** for the creation of `awdl0`.

## 4. What the kext needs in stage 1

**Nothing.** `RTLAwdInterface` must not override `handleIoctl` for these commands: the inherited
behavior (fall to `performGatedCommandIOCTL` → `RouteExternal` → 102 for out-of-range id) is
exactly what the real system does, and the daemon continues its sequence. Implementing a handler
for type 7708 would deviate from the Broadcom kernel without the daemon requiring it.

## 5. Pending

| Point | Result |
|---|---|
| semantics of type 7708 (what it would configure if it existed) | `?`; it is not an apple80211 id of `[1, 586]`, there is no handler |
| property feeding the ifname (keypaths `unk_10045FE58`/`unk_10045FE78`) | `?` (descriptors not resolved) |
| `sub_1000586D0` (variant with closure name) | no code callers (`?`) |
