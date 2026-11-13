# RegInfo +0x10/+0x40, `ifm_current`, the gate of the 94 and the flag of `handleIoctlGated`

Closure of points 7, 8 and 9 of `AWDL_STAGE1_IMPL_SPEC.md` §15 and of the gate of the handler of the 94,
read over the kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64). Each row with a VA is an instruction
seen in the disassembly of this session. The `?`s indicate the concrete search that was run and
found nothing.

## 1. The flag returning 6: `*(iop+128h)+52h` bit 0

The byte is the termination indicator of a private state block of the interface, stored at
`+0x128`. It is not a class object: `IO80211VirtualInterface::init` allocates it with
`IOMallocTypeImpl(kalloc_type_view_341)` (`0xffffff8002330630`), a zeroing typed allocation,
and stores the pointer at `0xffffff8002330635`. The bpf routines use it too.

| VA | Function | Instruction | Effect |
|---|---|---|---|
| `0xffffff8002330635` | `IO80211VirtualInterface::init(IO80211Controller *, ether_addr *, unsigned, char *)` | `mov [rbx+128h], rax` | stores the pointer of the object |
| `0xffffff8002330c5f` → `0xffffff8002330c66` | `IO80211VirtualInterface::terminate(unsigned)` | `mov rax, [rdi+128h]` → `mov byte ptr [rax+52h], 1` | **sets** the bit |
| `0xffffff8002330d26` → `0xffffff8002330d2d` | `IO80211VirtualInterface::stop(IOService *)` | `mov rax, [r14+128h]` → `mov byte ptr [rax+52h], 1` | **sets** the bit |
| `0xffffff8002330e46` → `0xffffff8002330e4f` | `IO80211VirtualInterface::bpfTap(unsigned, unsigned)` | `mov rcx, [rdi+128h]` → `test byte ptr [rcx+52h], 1` | consults |
| `0xffffff8002331d52` → `0xffffff8002331d5e` | `IO80211VirtualInterface::handleIoctlGated(...)` | `mov rcx, [r15+128h]` → `test byte ptr [rcx+52h], 1` | **the 6 of every apple80211 ioctl** |
| `0xffffff8002331ea2` → `0xffffff8002331ea9` | `IO80211VirtualInterface::terminating()` | `mov rax, [rdi+128h]` → `mov al, [rax+52h]` | reads the bit as the return value |

Consequence: the 6 is neither a capability nor a role rejection; it means the interface already entered
termination. It is set in `terminate` and in `stop`, and there is no `mov byte ptr [..+52h], 0` in the
range `0xffffff8002200000`-`0xffffff8002400000` clearing it: the bit starts at zero from the
allocation of `init` and only goes to 1 in `terminate` or `stop`.

For stage 1: do not call `stop` or `terminate` on `awdl0` before wifip2pd finishes its
sequence; a persistent 6 on the ioctls points to that bit, not to the dispatch.

## 2. `RegInfo+0x40`

`AWDL_INTERFACE_BIRTH.md` §2.2 documents the writer of the role 6 path: `AppleBCMWLANProximityInterface`
writes `RegInfo+0x40 = 4 or 6` according to `checkPropertyTrue(core, "IOPMResetPowerStateOnWake")`; the
STA path writes 0. No reader of `+0x40` appears in the decompiled bodies of:

| VA | Function |
|---|---|
| `0xffffff8002b10a84` | `IOSkywalkEthernetInterface::registerEthernetInterface(RegistrationInfo const *, PacketQueue **, unsigned, Pool *, Pool *, unsigned)` |
| `0xffffff8002b10b46` | `IOSkywalkEthernetInterface::registerEthernetInterface(RegistrationInfo const *, LogicalLink *, Pool *, Pool *, unsigned)` |
| `0xffffff8002b0a0d6` | `IOSkywalkNetworkInterface::registerNetworkInterface(RegistrationInfo const *, PacketQueue **, unsigned, Pool *, Pool *, unsigned)` |
| `0xffffff8002b0a204` | `IOSkywalkNetworkInterface::registerNetworkInterface(RegistrationInfo const *, LogicalLink *, Pool *, Pool *, unsigned)` |

`initRegistrationInfo` (`0xffffff8002b108e2`) does `bzero(a2+8, 0x128)` and writes: qword at `+0`
with `0x00000001300000001`, `+0x8` with the result of a slot (2 by default), `+0xC` with another
slot, `+0x14 = 256`, `+0x24 = 34`, `+0x28` with the result of a slot (32 by default), `+0x30`
with "en" by default, `+0x38` with the unit, `+0x44 = 46` and `+0x48`/`+0x4C = 1500`. In that body
**neither `+0x10` nor `+0x40` is written**: the `bzero` leaves them at zero.

`copyRegistrationInfo` (`0xffffff8002b109f0`) allocates 0x130 bytes with `kalloc_type_view_142`
(`0xffffff8002b45fc0`), stores the block at `*(this+0x118)`, copies the 0x130 with `memmove` and then
fixes `IOMACAddress` from `+0x108` via index 81 of the vtable. The two fields are not copied to
own ivars: any reader would have to go through `*(this+0x118)+off`.

Sweep of readers: in all the `IOSkywalk*`/`IO80211*` functions of
`0xffffff8002200000`-`0xffffff8002400000` and `0xffffff8002a00000`-`0xffffff8002c00000`, a load of
`[reg+118h]` followed within 10 instructions by a read of `[reg+10h]` or `[reg+40h]` only appears in
`IOSkywalkEthernetInterface::free` (`0xffffff8002b10896`) and in
`IOSkywalkEthernetInterface::ioctl_multicast` (`0xffffff8002b1127c`, `0xffffff8002b112a7`,
`0xffffff8002b112c6`, `0xffffff8002b1131a`, `0xffffff8002b1134b`). In those two methods the `+0x10`
read is the lock of the object, not the field of the `RegistrationInfo`.

`?` No reader of `RegInfo+0x10` nor of `RegInfo+0x40` has appeared; search run: the described
sweep, with a window of 10 instructions and offsets `+8`, `+0xC`, `+0x10` and `+0x40`.

## 3. `RegInfo+0x10`

The starting value is 0: `initRegistrationInfo` leaves it so by the `bzero` and the role 6 path
documented in `AWDL_INTERFACE_BIRTH.md` §2.2 does not touch it. The STA path writes 1. No read
of `+0x10` appears in the four functions of §2, nor in the sweep of §2. `?` The meaning of the field
remains unidentified; search run: `initRegistrationInfo`, `copyRegistrationInfo`, the four
registration functions and the sweep of readers of `*(this+0x118)`.

## 4. `ifm_current`

`IOSkywalkNetworkInterface::ioctl_gifmedia` (`0xffffff8002b0ae78`) does not read a direct `ifm_current`
field: it takes the media object at `this+0xC0` (`mov rax, [this+0xC0]` in the body), reads the
count at `+0x14`, allocates the table and lets slot `+0x948` fill it
(`0xffffff8002b0ae78`, call to slot `+0x948`).

Two more details are pinned from the decompiled body: the media descriptor is at `*(this+0xC0)`
and its count at `+0x14`; the table is filled by slot `+0x948`; and the copy into the `ifmediareq` of the
user is done under the lock of `*(this+0xC0)+0x10`.

Slot `+0x948` is `getSupportedMediaArray(unsigned *, unsigned *)`. In
`__ZTV23IO80211SkywalkInterface` (`0xffffff800246af00`) and `__ZTV23IO80211VirtualInterface`
(`0xffffff8002472b78`) it points to `IO80211SkywalkInterface::getSupportedMediaArray` (`0xffffff80022f8e5e`);
in `__ZTV25IOSkywalkNetworkInterface` (`0xffffff8002b3d2b8`) to the base version
`IOSkywalkNetworkInterface::getSupportedMediaArray` (`0xffffff8002b0b5f2`). The body of `0xffffff80022f8e5e` asks the core
(`*(this+0x120)+0x30`) for the collection via its slot `+0x890`, walks it with `OSCollectionIterator` and
writes into the table the value returned by slot `+0x120` of each object of the collection.

The media do not come from the `RegistrationInfo` nor from a per-interface ivar: they come from the media
collection of the core. `?` Which entry of that collection ends up in `ifm_current` (`ifmediareq+0x10`) and which
writers the collection has after the registration was not closed; search run: decompilation of
`ioctl_gifmedia` and of `getSupportedMediaArray` (`0xffffff80022f8e5e`).

## 5. Gate of the 94

| VA | Done |
|---|---|
| `0xffffff8002331d20` | `handleIoctlGated` is invoked from `handleIoctl` via slot `+0x850` of the controller. |
| `0xffffff80024619a8` (cell `+0x850` of `__ZTV17IO80211Controller`, `0xffffff8002461148`) | the slot resolves to `IONetworkController::executeCommand` (`0xffffff80026f51e0`): the action runs on the work loop of the controller. |
| `0xffffff80022f78ad` | On the `en2` route, the dispatch happens within `performGatedCommandIOCTL`. |
| `0xffffff80022f6e26` | On the user client route, `processBSDCommandGated` is the thunk of `performGatedCommandIOUC` (`0xffffff80022f6e30`), also gated. |

Consequence: on the `awdl0` route the dispatch runs within `executeCommand`, with the work loop
of the controller held. On the `en2` route (the one the 94 uses) the dispatch happens within
`performGatedCommandIOCTL`, and what gate it holds at that moment was not read (`?`). If it is the
same work loop: A `registerService(kIOServiceSynchronous)` within the handler waits for the
work loop to process the registration, and that work loop is the one running the handler: the
deadlock is possible. `kIOServiceAsynchronous` avoids the wait. `?` The exact point where
`executeCommand` takes and releases the gate was not read instruction by instruction, and the body of
`performGatedCommandIOCTL` (`0xffffff80022f779a`) contains no call to `executeCommand` nor
reference to a gate: on `en2` the gating comes from the caller, the `IOCommandGate` of the BSD client of
IOSkywalkFamily (§6).

## 6. `if_ioctl` of IOSkywalkFamily

`IOSkywalkNetworkBSDClient::if_ioctl` (`0xffffff8002b002e6`) gets the client with `ifnet_softc`
(`0xffffff8002b002f9`) and jumps to slot `+0x1C8` of the object at `client+0x90`
(`0xffffff8002b0032c`). That field is written in `IOSkywalkBSDClient::start`, `mov [rbx+90h], rax`
(`0xffffff8002afd448`), and is consumed by `ioctlHandler` (`0xffffff8002affd03`),
`runGatedAction` (`0xffffff8002b00658`) and `enableInterface` (`0xffffff8002b007a7`).

The object is an `IOCommandGate`: `IOSkywalkBSDClient::start` creates it with
`IOCommandGate::commandGate(this, 0)` (`0xffffff8002afd443`) right before the store, over the work loop
returned by slot `+0x688` of the client (`0xffffff8002afd42d`). In `__ZTV13IOCommandGate` slot
`+0x1C8` is `IOCommandGate::runAction`. Therefore every BSD ioctl, also the apple80211 one over `en2`,
runs `ioctlHandler` and `processBSDCommand` inside `runAction`, with that gate taken.

## 7. Searches run and pending

| Point | Search | Result |
|---|---|---|
| flag `+0x52` | byte ops with prior load of `[x+128h]` in `IO80211*` functions | 5 sites, table of §1 |
| readers of `RegInfo` | pseudocode of the 4 registration functions | no reads of `+0x10`/`+0x40` |
| current media | `getSupportedMediaArray` of `+0x948` | table from the media collection of the core; current entry unpinned (`?`) |
| `RegInfo+0x10`/`+0x40` | sweep of readers over `*(this+0x118)` | only `free` (`0xffffff8002b10896`) and `ioctl_multicast` (`0xffffff8002b1127c` …), and they read a lock; no readers (`?`) |
| slot `+0x948` | vtables of Skywalk/Virtual/IOSkywalkNetworkInterface | `getSupportedMediaArray` (`0xffffff80022f8e5e`, `0xffffff8002b0b5f2`) |
| block of `+0x128` | instructions before `0xffffff8002330635` | `IOMallocTypeImpl(kalloc_type_view_341)` at `0xffffff8002330630`: private struct, not class |
| object of `client+0x90` | store in `IOSkywalkBSDClient::start` + vtable `+0x1C8` | `IOCommandGate` (`0xffffff8002afd443`), slot `+0x1C8` = `runAction` |
