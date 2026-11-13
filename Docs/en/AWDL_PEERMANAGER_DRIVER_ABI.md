# Binary contract between an AWDL driver and IO80211AWDLPeerManager

This document fixes, byte by byte, the interface a third-party driver must implement for
`IO80211AWDLPeerManager` (IO80211Family, macOS 26, Darwin 25.6.0) to work as the AWDL protocol manager without
AWDL firmware underneath. Complements `AWDL_PEERMANAGER_SOFTWARE.md` (what the manager computes in
software) and `AWDL_EVENTS.md` (event channels toward userspace); only the
driver↔manager exchange is described here: which bytes are passed, in which order, who frees them and what the
manager checks in each direction.

Source: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`),
copy of the IDB in `Build/ida_awdl/task_awdl_peermgr/kc.i64` and its own copy in
`Build/ida_awdl/task_awdl_pm_abi/kc.i64` for the missing functions
(`reportDataPathEvents`, `ioctl_internal`, `bpfTapInput`, `handleActionFrame_rx`,
`routeToP2PInterface`, TLV builders, base `dequeueTxPacket`). All addresses are VAs of the
kernelcache and were read in the IDB. The offsets of ivars and structures belong to this
build: they are not ABI and are not frozen by any published header. Anything unconfirmed is marked `?`.

Slot convention: the offset of a cell is given as displacement from the `vptr`
(`cell = offset / 8`, counting from the destructor at cell 0). The `vptr` points 0x10 B after
the start of the `__ZTV` symbol, which holds two header entries; the cited vtable
(`0xffffff800245a868`) is already the address of the `vptr`.

---

## 0. Address map

| Symbol | Address | Role in the contract |
|---|---|---|
| `__ZTV22IO80211AWDLPeerManager` (vtable) | `0xffffff800245a868` | table that holds all the cited cells |
| `IO80211AWDLPeerManager::actionFrameInput` | `0xffffff800220e1ae` | cell 71, `+0x238`: action frame entry |
| `IO80211AWDLPeerManager::actionFrameProcessInput` | `0xffffff800220e8b0` | parsing and peer addition |
| `IO80211AWDLPeerManager::isValidAF` | `0xffffff800220fa64` | prior validation (length + OUI) |
| `IO80211AWDLPeerManager::actionFrameHasCorrectOUI` | `0xffffff8002210200` | OUI check on the buffer |
| `IO80211AWDLPeerManager::actionFrameProcessThread` | `0xffffff80021c3ebc` | drain of the internal ring |
| `IO80211AWDLPeerManager::reportDataPathEvents` | `0xffffff80021dd8f4` | cell 83, `+0x298`: driver events |
| `IO80211AWDLPeerManager::monitorTimeline` | `0xffffff80021e11ca` | validates event 41 |
| `IO80211AWDLPeerManager::logAWStart` | `0xffffff80021e1b42` | logging of event 41 |
| `IO80211AWDLPeerManager::topMasterChanged` | `0xffffff80021e3c08` | reaction to a top master change |
| `IO80211AWDLPeerManager::runElection` | `0xffffff80021e3e7c` | election triggered by event 43 |
| `IO80211AWDLPeerManager::dequeueTxPacket` | `0xffffff800221675e` | cell 85, `+0x2A8`: TX pull |
| `IO80211AWDLPeerManager::getUnicastPackets` | `0xffffff8002216a4e` | cell 46, `+0x170` |
| `IO80211AWDLPeerManager::getMulticastPackets` | `0xffffff8002216a2e` | cell 45, `+0x168` |
| `IO80211PeerManager::dequeueTxPacket` | `0xffffff8002250554` | reused base implementation |
| `IO80211PeerManager::getUnicastPackets` | `0xffffff800224ffdc` | base implementation |
| `IO80211PeerManager::getMulticastPackets` | `0xffffff800224fe4e` | base implementation |
| `IO80211PeerManager::ioctl_internal` | `0xffffff80022524e8` | emission of internal ioctls |
| `IO80211PeerManager::ioctl_internal_gated` | `0xffffff800225238a` | decodes `ioctl_internal_req` |
| `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*,...)` | `0xffffff8002331070` | **real AF injection path** |
| `IO80211VirtualInterface::dequeueTxPackets` | `0xffffff80023313b2` | **real TX pull path** |
| `IO80211InfraInterface::routeToP2PInterface` | `0xffffff800236364e` | event forwarding to the manager |
| `IO80211SkywalkInterface::reportDataPathEventsGated` | `0xffffff80022f8d1a` | event forwarding to the manager |
| `AppleBCMWLANProximityInterface::handleActionFrame_rx` | `0xffffff800171795c` | header builder on Broadcom |
| `IO80211AWDLPeerManager::enableAWDL` | `0xffffff80021dcf58` | `awdl_cap` read and 117 |
| `IO80211AWDLPeerManager::setSyncFramePeriod` | `0xffffff80021ed024` | ioctl 116 |
| `IO80211AWDLPeerManager::updateSyncFrameTemplate` | `0xffffff80021dc5b2` | ioctl 151 |
| `IO80211AWDLPeerManager::programChipWithNewMaster` | `0xffffff80021ec4b6` | ioctls 229, 134, 155 |
| `IO80211AWDLPeerManager::initFWElectionParams` | `0xffffff80021ed200` | ioctl 155 when enabling |
| `IO80211AWDLPeerManager::setChannelSequenceUnconditional` | `0xffffff80021cb69a` | ioctl 129 |
| `IO80211AWDLPeerManager::programMasterChannels` | `0xffffff80021d494a` | ioctls 132 and 205 |
| `IO80211AWDLPeerManager::setAFTXMode` | `0xffffff80021f7d06` | ioctl 208 |
| `IO80211AWDLPeerManager::sendUnicastMI` | `0xffffff8002213c0a` | ioctl 149 |
| `IO80211AWDLPeerManager::buildServiceParamTlv` | `0xffffff80022118a6` | TLV 6 of the template |
| `IO80211AWDLPeerManager::buildAWDLElectionParamsVer2` | `0xffffff8002210a3c` | TLV 24 of the template |
| `IO80211AWDLPeerManager::buildSyncTreeTlv` | `0xffffff80022112fe` | TLV 20 of the template |
| `IO80211AWDLPeerManager::cleanUpForPeerRemovedFromNDP` | `0xffffff8002214cf2` | peer cleanup (not event-driven) |

---

## 1. `actionFrameInput`: AWDL action frame injection

### 1.1 Signature and calling convention

```c
// cell 71 of vtable 0xffffff800245a868, offset +0x238
int IO80211AWDLPeerManager::actionFrameInput(IO80211Buffer *buf,   // rsi
                                            uint32_t dlt,          // edx
                                            void *hdr,             // rcx  (apple80211_act_frm_hdr)
                                            size_t hdrLen);        // r8
```

Verified at `0xffffff800220e1ae`: the function **validates nothing on entry** (neither `buf`, nor `hdr`, nor
`hdrLen`); on all paths it returns 0. The first real check is the copy of the header to
the internal ring: if `hdrLen >= 0x49` a deliberate `ud1` instruction is executed (same pattern in
`0xffffff800220e1ae` and in `actionFrameProcessThread` `0xffffff80021c3ebc`), i.e. **a header
longer than 72 B triggers a kernel trap**. The limit is exactly
`sizeof(struct apple80211_act_frm_hdr) == 0x48`.

`dlt` is propagated uninterpreted up to `IO80211VirtualInterface::sendToBpfTap`; the value used by the
Broadcom path is 12 (`0xC`, DLT IEEE802.11). The manager does not compare it.

### 1.2 `IO80211Buffer`

`IO80211Buffer` is a 24 B `OSObject` (`OSObject_typed_operator_new(&IO80211Buffer_ktv, 24)`,
vtable `0xffffff8002473e90`). Layout verified in `initWithOptions`
(`0xffffff800233b8a2`), `setLength` (`0xffffff800233bcbe`), `getIndex` (`0xffffff800233bcf6`) and
`writeBytes` (`0xffffff800233bc74`):

```c
struct IO80211Buffer {              // 24 B
    void        **vptr;             // +0x00
    uint32_t      retainCount;      // +0x08 (OSObject)
    uint32_t      pad;              // +0x0c
    struct io80211_buffer_desc *desc; // +0x10
};

struct io80211_buffer_desc {        // kalloc_type_view_106
    uint64_t                unknown0;  // +0x00 ?
    void                   *slab;      // +0x10
    struct IO80211Buffer   *owner;     // +0x18
    uint32_t                length;    // +0x20
    uint32_t                index;     // +0x24
    /* total size ? */
};
```

The manager **never reads those fields**: it uses the vtable exclusively. Relevant cells (offset from
`vptr`, read in the vtable at `0xffffff8002473e90`):

| Offset | Method | Use in the contract |
|---:|---|---|
| `+0x118` | `returnBuffer` | returns the buffer to its pool; the manager calls it when done |
| `+0x130` | `getLength` | `isValidAF` uses it in the error message; `actionFrameProcessInput` calls it |
| `+0x138` | `setLength` | used by the producer (Broadcom) before injecting |
| `+0x140` | `getCapacity` | not used by the manager |
| `+0x148` | `getCursor` (not const) | the producer gets the destination cursor |
| `+0x150` | `getCursor` (const) | `isValidAF` gets the source cursor |
| `+0x158` | `getBytesNoCopy` | `actionFrameProcessInput` gets the base pointer |
| `+0x178` | `writeBytes` | not used by the manager |

A *cursor* is a 16 B pair copied onto the caller's stack:

```c
struct IO80211BufferCursor {
    const void *ptr;   // +0x00
    uint64_t    len;   // +0x08
};
```

`isValidAF` (`0xffffff800220fa64`) calls `+0x150` with a 16 B destination and requires both fields
to be non-zero; `actionFrameHasCorrectOUI` (`0xffffff8002210200`) additionally requires `len >= 0x10`.

**Ownership**: `actionFrameInput` consumes the buffer. On the path with its own thread it saves it in the
ring and `actionFrameProcessThread` (`0xffffff80021c3ebc`) calls `returnBuffer` (`+0x118`) when
done; on the discard paths it returns it itself before returning. The caller must not
touch the buffer after the call nor free it.

### 1.3 `struct apple80211_act_frm_hdr` (72 B)

**Packed** structure (there are mid-word writes in `handleActionFrame_rx`). Fields
read by the manager and fields written by Broadcom:

| Offset | Type | Field | Who reads/writes it |
|---:|---|---|---|
| `+0x00` | `uint8_t[6]` | source MAC of the AF | written at `0xffffff800171795c` (`*(u32*)(ev+24)`, `*(u16*)(ev+28)`); read at `0xffffff800220e8b0` (`findPeer`, `addAWDLPeer`) and compared against the parent at `+37237` |
| `+0x06` | `uint16_t` | no use (`?`) | stays zero |
| `+0x08` | `uint8_t[4]` | no use (`?`) | stays zero |
| `+0x0c` | `apple80211_channel` (12 B) | channel of the AF | written with `ChanSpecConvToApple80211Channel` at `0xffffff800171795c` (band `0x08`/`0x10`/`0x2000`, width `0x02`/`0x04`/`0x400`/`0x800`, DFS `0x100`); the flags are read by `IO80211Peer::reportRssi` (`0xffffff8002242c4e`, from `actionFrameReport` `0xffffff80021afbb0`): `0x10` updates the 5 GHz RSSI average (`+1188`), `0x08` the 2.4 GHz one (`+1180`); `wifip2pd` derives the band from them |
| `+0x18` | `uint8_t[16]` | zeros | — |
| `+0x28` | `uint32_t` | RX rate (`rate` of the event) | written at `0xffffff800171795c`; not read by the manager |
| `+0x2c` | `int32_t` | **RSSI of the AF in dBm** (`rssi` of the event) | read in `actionFrameProcessInput` (`mov eax, [r15+2Ch]` at `0xffffff800220ecac` and `0xffffff800220ecce`) and compared **signed** with the edge RSSI threshold `ivars+10268` |
| `+0x30` | `uint32_t` | `mactime` of the event, µs `?` | written at `0xffffff800171795c`; not read by the manager |
| `+0x34` | `uint8_t[10]` | zeros | — |
| `+0x40` | `uint32_t` | `1` (version 2 only) | written at `0xffffff800171795c`; not read |
| `+0x44` | `uint8_t[4]` | 4 B copy of the event (v2 only) | written at `0xffffff800171795c`; not read |

Notes:

- `+0x2c` is the RSSI, not a timestamp (correction of the previous version of this note). The
  three values of `+0x28/+0x2c/+0x30` come from `struct brcmf_rx_mgmt_data` (`version`, `chanspec`,
  `rssi`, `mactime`, `rate`, all big-endian), which follows the 48 B event header
  (`brcmf_event_msg_be`). That is why on v1 `rssi` is at `ev+52`, `mactime` at `ev+56` and `rate` at
  `ev+60` (public definitions of brcmfmac, `fwil_types.h` and `fweh.h`; that Apple's firmware uses
  the same structure is consistent with the offsets, `?`). Broadcom saves them already in host order
  (`_byteswap_ulong`).
- `ivars+10268` is the **firmware edge RSSI threshold**: `initParamsToDefaults` sets it to −70
  (`mov dword ptr [rcx+281Ch], 0FFFFFFBAh` at `0xffffff80021c7cdf`), it is changed by
  `determineAndSetEdgeRssiThreshold` and `setAwdlElectionRssiThresholds`, `getEdgeRssi` returns it, and
  `printState` prints it as "Firmware edge RSSI threshold". In `actionFrameProcessInput`, with a
  known peer, `cmp eax, [rcx+281Ch]` / `jge` (`0xffffff800220ecb4`) lets the AF pass if the RSSI
  reaches the threshold; if not, the peer is freed and the frame discarded. With an unknown sender,
  `cmp`/`jl` (`0xffffff800220ecd6`) discards the AF without calling `addAWDLPeer`. A driver injecting
  with `+0x2c = 0` always passes the filter; it must put the real RSSI of the frame for the manager
  to ignore distant peers.
- The manager **does not** get the frame body from this header: the body travels in the
  `IO80211Buffer`. The header is metadata only (MAC, channel, times).
- Destination MAC, BSSID and RSSI do not appear in any manager access to this header; if the driver
  needs them for its own datapath it must carry them in its own copy.

### 1.4 Internal ring of 104 B per entry

With boot-arg `wlan.splitAWDLAFThread` active (default; bit 0 of `ivars+16880`), `actionFrameInput`
enqueues into its own ring:

```c
struct af_ring_entry {          // 104 B (0x68)
    IO80211Buffer *buf;         // +0x00
    uint32_t       dlt;         // +0x08
    uint8_t        pad[4];      // +0x0c
    uint8_t        hdr[72];     // +0x10  copy of apple80211_act_frm_hdr
    uint64_t       hdrLen;      // +0x58
    uint64_t       enqueueTime; // +0x60  mach_absolute_time
};
```

Ring metadata in the AWDL ivar block (`*(manager+0x20)`):

| Offset | Field |
|---:|---|
| `+16840` | ring base (`u8 *`) |
| `+16848` | head (`u32`) |
| `+16852` | tail (`u32`) |
| `+16856` | current size (entries) |
| `+16860` | mask (`size-1`) |
| `+16868` | overflow flag |
| `+16872` | last activity mark |
| `+10408` | `IO80211TimerSource` of thread `actionFrameProcessThread` |
| `+10416` | "thread armed" flag |

Head advance, inside `0xffffff800220e1ae`: `head = (head + 1) & mask`. If the ring is full, it sets
`+16868 = 1`, arms the timer (`vtable +464` over `ivars+10408`) and sends the frame to the BPF tap without
processing. `growAFRing` (`0xffffff800220dd0a`) and `shrinkAFRing` (`0xffffff800220dfca`) adjust the size.

With the boot-arg disabled, `actionFrameInput` calls `actionFrameProcessInput` directly and only
sends the frame to the BPF tap if the parsing returned "invalid".

### 1.5 How `AppleBCMWLANProximityInterface` builds the call today

The real chain is **indirect through the BPF tap**, not a direct call to the manager:

1. `AppleBCMWLANProximityInterface::handleActionFrame_rx` (`0xffffff800171795c`) receives the event
   `wl_event_rx_frame_data` and validates lengths: `len >= 0x30`, `*(u32*)(ev+20)` (version) and the
   variable prefix (16 B for v1, 24 B for v2).
2. It builds on the stack a 72 B `apple80211_act_frm_hdr`: copies the source MAC from `ev+24`/`ev+28`,
   converts the event channel (`AppleBCMWLANChanSpec::getAppleChannelSpec` +
   `ChanSpecConvToApple80211Channel`) into the `apple80211_channel` of `+0x0c`, and fills the three
   big-endian `uint32_t` of the event with `_byteswap_ulong` (they end in host order): on v1
   `+0x2c ← ev+52` (rssi), `+0x30 ← ev+56` (mactime), `+0x28 ← ev+60` (rate); on v2 `+0x2c ← ev+56`, `+0x30 ← ev+60`, `+0x28 ← ev+64`. On v2 it adds `1` at
   `+0x40` and copies 4 B of `ev+68..71` to `+0x44`.
3. It requests an `IO80211Buffer` from the management frame pool (`getMgmtFramePool` → `alloc` →
   `+0x148 getCursor` → `IO80211BufferCursor::copyFrom` → `+0x138 setLength(len)`) and copies the frame
   body (the event minus its prefix) inside.
4. It calls `IO80211VirtualInterface::bpfTapInput(iface, buf, 12, &hdr, 72)` (`0xffffff8002331070`).
5. `bpfTapInput` takes the peer manager at `*(*(iface+288)+40)` and, **only if `dlt == 12`** and the manager
   exists, executes `(*(pm->vptr + 0x238))(pm, buf, 12, hdr, hdrLen)`; if that call returns 0 the
   buffer is already consumed and nothing more happens. If it returns non-zero, or there is no manager,
   `sendToBpfTap` is called and the buffer is returned (`+0x118`).

Consequence for a custom driver: **the supported door is `bpfTapInput` with DLT 12**, not cell
71 directly. Calling cell 71 requires replicating the buffer ownership contract (the manager
returns it to its pool, so the buffer must come from a valid pool and must not be reused).

**Variant with `mbuf`, without own pool.** The exported overload
`IO80211VirtualInterface::bpfTapInput(mbuf*, unsigned, void*, size_t)` (`0xffffff8002330e7a`, no
callers in the kernel) does the producer's work:
1. It gets the controller through cell `+0xBA8` of the interface (`getController`) and, from its private
   block (`+288`), the pool `+2904`.
2. It requests an `IO80211Buffer` from the pool (cell `+280`).
3. It copies the mbuf or the mbuf chain (`writeBytes` `+0x178`, or `getBytesNoCopy` `+0x158` and
   `memmove`) and sets the length (`setLength` `+0x138`).
4. It calls the `IO80211Buffer` overload (`0xffffff8002331070`).

It does not free the mbuf: it stays owned by the caller. It does not check the buffer capacity when copying a
chain, so the frame must fit in 2048 B. If the pool does not exist, it returns without doing anything.

The pool is created by `IO80211Controller::allocFramePool` (`0xffffff800229280a`, write at
`0xffffff800229289e`), called from `IO80211Controller::start` (`0xffffff8002292451`). It is the `mngPool`
of `getActionFramePoolCapacity()` buffers of 2048 B; the cell is `0xDB0` (slot 438), whose
base implementation (`0xffffff800229a1e2`) returns 256. `IO80211Controller::getMgmtFramePool`
(`0xffffff800229a1ee`) returns it. The project's own layer calls `IO80211Controller::start` and does not overwrite
slot 438 (`Source/AirLayer/RTLAirLayer.cpp:1516`, `Source/IO80211/IO80211_25_6_0.h:863`), so
the pool exists. For each received action frame an mbuf with the body from the category byte
and the 72 B header of §1.3 is enough: `awdl0->bpfTapInput(m, 12, &hdr, 72)` and then `mbuf_freem(m)`.

### 1.6 What the manager validates and what it discards

`isValidAF` → `actionFrameHasCorrectOUI` (`0xffffff8002210200`) on the **buffer cursor**, not
on the header:

- `hdrLen > 0x47` and `buf != NULL` and `hdr != NULL`, else: log `"Invalid frame/header, headerLen %lu"`
  and discard.
- Cursor with non-null `ptr` and `len` and `len >= 0x10`, else: discard.
- Frame bytes in the cursor: `[1] == 0x00`, `[2] == 0x17`, `[3] == 0xF2`, `[4] == 0x08`,
  `[5] != 0`, else: `"BAD OUI in AF"` and discard (the full frame is dumped to the log). Byte
  `[0]` is not checked (`?` on its meaning: category/action of the frame).
- `*(u32*)(hdr+44) < *(u32*)(ivars+10268)` → silent discard in `actionFrameProcessInput`.

An accepted AF creates or updates the peer (`IO80211AWDLPeer::withAddressAndManager` →
`initWithAddressAndManager`), is parsed TLV by TLV in `IO80211AWDLPeer::actionFrameReport`
(`0xffffff80021af6be`) and, if applicable, the UMI is enqueued and the election is re-armed.

---

## 2. `reportDataPathEvents`: datapath events of the driver

### 2.1 Inputs and return convention

```c
// cell 83 of the vtable, offset +0x298
int IO80211AWDLPeerManager::reportDataPathEvents(uint32_t id, void *data, size_t len);
```

Two real callers, both with the same signature and the same reading of the return:

- `IO80211SkywalkInterface::reportDataPathEventsGated` (`0xffffff80022f8d1a`):
  `pm = *(*(skywalk+288)+40)`; if `pm == NULL` **or** the call returns **non-zero**, the
  event is published to userspace with `postMessageInternal`. Return 0 ⇒ event consumed by AWDL.
- `IO80211InfraInterface::routeToP2PInterface` (`0xffffff800236364e`): forwards to the manager only the
  ids of the whitelist **2, 3, 4, 9, 66, 80, 125, 126, 127, 139, 160, 198, 242** (plus the
  routing to the NAN manager), resolving the manager by `*(*(infra+37*8)+360)`
  → `*(*(iface+288)+944)` → `safeMetaCast(..., IO80211AWDLPeerManager::gMetaClass)`
  and calling `(*(pm->vptr + 0x298))(pm, id, data, len)`.

At the end of the function (`0xffffff80021dd8f4`, epilogue) it calls
`IO80211PeerManager::recordDataPathEventsProcessing(handled, bySubclass, !bySubclass)`
(cell 84, `0xffffff800224de5a`) and returns the computed value. The **return value has only
that gate meaning**: 0 = consumed, ≠0 = let it pass to userspace.

`data` may be `NULL` for several ids (explicit asserts on 41/42/43/49/160); ids 41, 42, 43, 46
and 47 do not check `len`.

### 2.2 Event 41 — `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START`

`case 41` branch of the switch of `0xffffff80021dd8f4`, with preconditions:

- Early exits, in this order: reset active with AWDL off, AWDL off (`+16096` bit 1 at 0)
  or disable in progress (`+414` bit 0) → it logs
  `"Exiting because of isAwdlResetActive=..., isAwdlOff=... isAwdlDisableInProgres=..."` and
  returns 0 without touching anything.
- `data == NULL` → `"APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START no param!!"` and return 0.

Payload (36 B read; the manager does not validate `len`):

| Offset | Type | Field | Destination / use |
|---:|---|---|---|
| `+0x00` | `uint16_t` | `AwNumber` | `monitorTimeline` (`0xffffff80021e11ca`) compares it with the previous one; in the same 41 branch it is copied to `ivars+16104` as `uint32_t` |
| `+0x02` | `uint8_t[6]` | reserved, copied as is | full 36 B copy to `ivars+776..811` |
| `+0x04` | `uint32_t` | firmware sync state: 0 Stopped, 1 Master, 2 Slave, 3 NeMaster (names of `printState`, `0xffffff800222220e`) | compared with `ivars+17028`; if changed it saves `mach_absolute_time()` at `ivars+42728` and copies the value. The same ivar is written by `setAwdlSyncState` after an accepted 134 and set to 0 by `disableAWDL`, `reset` and `awdlChipReset`; `logAWStart`/`logAWEnd` write `Start:`/`End:` capitalized when it equals 1, and `setInfraTxState` only acts if it is different from 0 |
| `+0x08` | `uint8_t[6]` | MAC of the current master | `monitorTimeline` (`memcmp` with `ivars+784`) and log |
| `+0x0e` | `uint8_t[6]` | MAC of the top master (root) | `monitorTimeline` (`memcmp` with `ivars+790`); in the 41 branch it is copied to `ivars+37231` if changed and `ivars+16289` is active |
| `+0x14` | `uint32_t` | `FwAwStartTime` **big-endian** | `monitorTimeline` does `_byteswap_ulong` and compares it with the expected |
| `+0x18` | `uint32_t` | version of `apple80211_channel` | `ivars+42824` (qword `+0x18..+0x1f`, the one `getMyCurrChanInfo`, `0xffffff80022143b2`, returns) |
| `+0x1c` | `uint32_t` | channel number of the window | `ivars+42828`; event 43 compares it with its channel table |
| `+0x20` | `uint32_t` | channel flags of the window | `ivars+42832`; bits `0x2018` are compared by `sanityCheckAWStart`, `handleUmiTimer` and event 43 |

Immediate actions after `monitorTimeline`, all inside the 41 branch of
`0xffffff80021dd8f4`:

- `ivars+772 = 1` → `inAvailabilityWindow()` (`0xffffff8002219af4`) starts returning true.
- Copies 36 B to `ivars+776`, and `+0x18`/`+0x20` to `ivars+42824`/`+42832`.
- `lockDataPath` (vtable of `datapath+280`, via `*(manager+24)+120`), `ivars+0 = 1`, `ivars+408 = 0`.
- Computes the slot index: `ivars+16128 = min(15, (awNumber % ((+42867+1)*+42820)) / (+42867+1))`
  and calls `(*(iface->vptr + 3768))(iface, slot, remainder)` with the interface `*(*(manager+24)+1376)` —
  slot and sub-slot within the window (`?` on the method's name).
- `ivars+16108 = awNumber & 0xFFFFFFC0`; if changed vs `+16112`, it saves `mach_absolute_time()`
  at `+16120` and updates `+16112`.
- `logAWStart` + `sanityCheckAWStart` (`0xffffff80021e21de`): expects the channel at `ivars+12*slot+42872` and
  the flags at `+42876`; if the channel differs, or `(expected_flags ^ ivars+42832) & 0x2018` is not 0, it
  increments `ivars+64328`, which `monitorAWDLState` reads to print `mismatch of channel sequence with Firmware`.
  It keeps other counters: `+64324` back-to-back starts, `+64336` back-to-back sequence, `+64340` start events
  that did not arrive in slots with a channel, `+43880` unexpected sequence number.
- `ivars+42696 = mach_absolute_time()` (window start in host time).
- If the current slot returned by `getCurrentAWSlot` (cell 94, `+0x2F0`) is 0 and `ivars+66376 != 0`:
  if less than 64 ms passed since the previous one, it logs `"slot 0 start too soon (%dms), ignore"`
  and **abandons the event with return 0**.
- `postSyncStateChanged` is **not** called here; the sync state is copied at `+17028` from `+0x04`
  (see below) and only published on change.

### 2.3 What `monitorTimeline` validates and what happens if it does not match

`monitorTimeline` (`0xffffff80021e11ca`, called from the event 41 branch of
`0xffffff80021dd8f4`) keeps at `ivars+776..811` the last accepted `apple80211_availability_window_start`:

| Ivar | Mirror field |
|---:|---|
| `+776` | qword `+0x00..+0x07` (AwNumber and reserved) |
| `+784` | master MAC |
| `+790` | top master MAC |
| `+796` | `FwAwStartTime` (re-read with `_byteswap_ulong`) |
| `+808` | dword `+0x20` |
| `+812` | "first sample pending" flag |

Algorithm, in order:

1. **First sample** (`ivars+812` bit 0): copies the 36 B, clears the flag (`+812 = 0`) and returns
   `false` (nothing to compare). Nothing is logged.
2. New `AwNumber` (`+0x00`) vs previous (`+776`), new `FwAwStartTime` (`bswap(+0x14)`) vs
   previous (`bswap(+796)`), `masterChanged = memcmp(data+8, ivars+784, 6)`,
   `topMasterChanged = memcmp(data+14, ivars+790, 6)`. Always traces:
   `"AwNumber=%d->%d, FwAwStartTime=%u->%u, Master=...(Changed=%d), TopMaster=...(Changed=%d)"`.
3. **Master change**:
   - If `ivars+10304` (master change pending apply) is at 0 and the received master does **not**
     match the expected (`ivars+37465`): log only
     `"Waiting for Master=..., recieved Master=..."`.
   - If `ivars+10304` is at 0 and it matches: log `"Detected that host Master change was applied"`,
     `ivars+10304 = 1` and `topMasterChanged = false` for the rest of the function.
   - If `ivars+10304` is at 1 and `masterChanged`: log
     `"Detected Master change with no reason, Master=...->..."` and marks `unexpectedMaster = true`
     (variable `v17` of the decompiled code), which later decides the discontinuity notice text.
   - If there is no master change but `topMasterChanged`: log
     `"Detected TopMaster change without master change, TopMaster=...->..."` and
     `unexpectedMaster = false`.
   - No changes: `unexpectedMaster = true`.
4. **`AwNumber` continuity**. Two expected values are computed from the previous `AwNumber`
   and from the current channel sequence (`+42872`, 12 B entries) and the previous one (`+43256`):
   - base index `idx = (aw_prev >> 2) & 0xF`;
   - it searches, circularly modulo 16, the next occupied slot in each sequence
     (`v23` in the current, `v25` in the previous); "occupied" = channel different from zero at `entry+4`;
   - `expected1 = aw_prev + 4*((v23 - idx) mod 16)`, `expected2 = aw_prev + 4*((v25 - idx) mod 16)`,
     with 16-bit overflow detection (`"expectedAwNumber1/2 overflowed"`).
   - If the received `AwNumber` is neither of the two: log
     `"Detected AW Number discontinuity without master change, receivedAwNumber=%d, expectedAwNumber=%d or %d"`
     or `"...with master change"` depending on `unexpectedMaster`, and in the first case it dumps the two
     complete channel sequences (`prevChanSeq` / `newChanSeq`, 16 entries).
   - **There is no correction or resynchronization**: logging only. The state is updated all the same.
5. **Time drift**. With the accepted `AwNumber`: `expectedStart = bswap(+796) + ((4*delta) << 14)`
   where `delta` is the slot jump used (i.e. `delta * 65536` firmware units); if
   `|FwAwStartTime - expectedStart| >= 0x1401` (5121 µs)
   it logs `"Detected AW start time inaccuracy: expetedAwStartFwTime = %u, currAwStartFwTime = %u, delta = %d [usec]"`.
   Again, trace only: nothing is reprogrammed.
6. Saves the new 36 B at `+776..811` and returns `topMasterChanged`.

The return of `monitorTimeline` is only used to decide whether the top master changed; the consequence
is in the caller: if `ivars+10307` (election fixed) is active it calls `topMasterChanged`
directly; if not, when `ivars+16289` is active and the payload's top master MAC differs from
`ivars+37231`, it overwrites `+37231..37236` with `data+14` and calls `topMasterChanged`
(`0xffffff80021e3c08`).

### 2.4 Event 42 — `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSIONS_START` (`?` name)

`case 42` branch of the switch of `0xffffff80021dd8f4`:

- If `ivars+66118` bit 0 (reset) → log `"Exiting because reset is active"` and return 0.
- `data == NULL` → same message `"...no param!!"` as the 41.
- Optional trace (`shouldLog 0x4000`) with `xten:`/`Xten:` and the firmware time.
- **Single write**: `ivars+408 = *(u32*)data`. That ivar is the packets/window counter that
  `dequeueTxPacket` compares with 4 (§4.2) and that event 41 sets to 0.
- Return 0.

Payload: only `+0x00` is read (`uint32_t`); `+0x04` is used in the trace as a big-endian firmware
timestamp. `len` is not checked.

### 2.5 Event 43 — `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSIONS_END`

`case 43` branch of the switch of `0xffffff80021dd8f4`. Effects, in order:

1. `ivars+772 = 0` (window end), `ivars+757 = 0`, `ivars+0 = 0`,
   `ivars+16400 = mach_absolute_time()`.
2. If `ivars+66118` bit 0 → log and return 0 with nothing else.
3. **Election**: `if (shouldRunElection()) runElection();` (`0xffffff80021e3cca` /
   `0xffffff80021e3e7c`), still without the datapath locked. It is the only periodic trigger of the
   election besides the parsing of a peer's Sync Params.
4. `lockDataPath`; if it fails it logs `"Event:%s, lockDataPath failed"` and exits with return 0.
   On success, `ivars+17024 = 0`.
5. `ivars+42676 = *(u32*)(data+0x0c)` (dword of the payload, use `?`).
6. **CCA** (only if capability `CCA_STATS` at `ivars+16577` and `*(u16*)(data+0x10) == 1`):
   with `dw = 10 * *(u32*)(data+0x18)` (0 ⇒ three counters at 0 are used), it computes
   `a = *(u32*)(data+0x1c)/dw`, `b = *(u32*)(data+0x20)/dw`, `c = *(u32*)(data+0x24)/dw`,
   `total = a+b+c`, and calls `reportP2PCCA(channel = ivars+42828, total, a, b, c)`.
   Then it updates the table of 3 entries of 12 B at `ivars+53308`
   (indexed by channel) and, if applicable, `airplayStatsBuildAvgCCAHistogramForChannel` /
   `rtgStatsBuildAvgCCAHistogramForChannel`.
7. `logAWEnd` + `sanityCheckAWEnd` (`?`), `unlockDataPath`.
8. If `ivars+17032` (data mode) ≠ 0: `IO80211Controller::setCurrentChannel`.
9. Piggyback scan: if `ivars+42820 != 0` and `ivars+17032 != 0` and the sequence arithmetic matches,
   with `ivars+10480 == 3` it sends the ioctl `0xD3802869C8` (type 211, "stop piggyback scan") and calls
   `setScheduleState`; with `== 2` it calls `runPiggybackScanStateMachine`.

Payload (40 B read; `len` is not checked):

| Offset | Type | Field |
|---:|---|---|
| `+0x00` | `uint8_t[12]` | common header with event 41 (not used here) |
| `+0x0c` | `uint32_t` | value copied to `ivars+42676` (`?`) |
| `+0x10` | `uint16_t` | "there is CCA" flag (must be 1) |
| `+0x14` | `uint32_t` | reserved |
| `+0x18` | `uint32_t` | time base in 0.1 µs units (`dw = 10*x`) |
| `+0x1c` | `uint32_t` | CCA counter 1 |
| `+0x20` | `uint32_t` | CCA counter 2 |
| `+0x24` | `uint32_t` | CCA counter 3 |

### 2.6 Event 47 and the rest of the ids

**Event 47 is not consumed by the manager.** The `case 47` branch of the switch of
`0xffffff80021dd8f4` shares a destination with 44, 45, 48 and others (`goto LABEL_52`), which delegates to
`IO80211PeerManager::reportDataPathEvents` (`0xffffff800224cd4c`); that function has no branch
for 47 either (its own ids are 1, 2, 3, 4, 7, 12, 13, 39, 40, 49…58, 85, 87, 160, 198, 204, 227–233 and
the rest fall in the `default`), so the event exits with return ≠ 0 and ends up published to
userspace by `postMessageInternal`. `cleanUpForPeerRemovedFromNDP` (`0xffffff8002214cf2`) **is not
triggered by any event**: its only xref is `IO80211AWDLPeer::setPeerNotReachable`
(`0xffffff80021aa717`, internal call), a correction regarding `AWDL_PEERMANAGER_SOFTWARE.md` §5.

Ids the AWDL manager does handle and their effect:

| Id | Effect | Minimum payload read |
|---:|---|---|
| 41 | §2.2 (window start) | 36 B |
| 42 | §2.4 (`ivars+408 = data[0]`) | 4 B |
| 43 | §2.5 (window end, CCA, election) | 40 B |
| 46 | if `data[0] != 0`: nothing (only forwarded to userspace). With `data[0] == 0`: clears `+16096` bit 1, `+17028` and `+10480`, calls `setEnableState(false)` and, if it was enabled before, `postSyncStateChanged` | 4 B |
| 49 | `APPLE80211_M_RESET_INTERFACE`: assert `len == 4`, saves `data[0]` at `ivars+16100`, forwards to the base as id `0x31`; with `data[0] != 0` it marks AWDL recovery and reconfigures | 4 B |
| 51 | peer cache, see below (handled by `IO80211PeerManager::reportDataPathEvents`) | opcode at `+4`, MAC at `+8` |
| 58 | interface state (`IFACE_STATE`) → `setDataPathState` | `u32` |
| 64 | SDB mode changed (assert `len == 8`) → rebuilds and pushes down the sync template and the MI | 8 B |
| 82 | roam completed → `setSteerComplete`, `setScheduleState` | `u32` |
| 160 | assert `len == 8`; `APPLE80211_M_SDB_MODE_CHANGED` | 8 B |
| 179, 182, 172, 227–233, 243 | consumed without side effect | — |
| 183 | DFS suspend/resume (`data[2]`, `data[3]`) | 4 B |
| 198 | 6 GHz scan overlay → `setChannelSequenceUnconditional` | `u32` |
| 204 | dwell start (`data[2] != 0`) | 4 B |

**Event 51: the answer to ioctl 143.** `AppleBCMWLANCore::completePeerCacheControl` (`0xffffff800165b8b6`) posts it
with 464 B, opcode at `+4` (1 add, 3 delete) and MAC at `+8`. `IO80211PeerManager::reportDataPathEvents` hands it to
`handleCacheControlMessages` (`0xffffff800229ce24`):

- With opcode 1, `handlePeerAddIndication` (`0xffffff800229cf14`) wakes whoever waits on operation 8
  (`kApplePeerCacheOperationCompleted`), adds the peer to the cached list and `setStateForCachedPeer`
  (`0xffffff8002242830`) leaves `peer_ivars+601 = 1` and `+600 = 0`. `handleUmiTimer` requires that state.
- With opcode 3, `handlePeerDeleteIndication` (`0xffffff800229d3bc`) lowers `_currentPeerCacheSize`, the counter that
  `sendIoctlToCachePeer` (`0xffffff800229e458`) raises when it sends the add. It only falls here:
  `deletePeerFromFWCache` (`0xffffff800229d798`) does not touch it. With the counter at the maximum of 8 (ioctl 130),
  IO80211 logs `No space to cache peer` and sends no add.

If a delete arrives while an add is unconfirmed, `deletePeerFromFWCache` does not send 143 and leaves
`peer_ivars+6 = 1`; `handlePeerAddIndication` sends it once the add is confirmed. Measured in
`AWDL_PAR_VIRTUAL.md` §9.6.

The ids not listed above nor in the whitelist of `routeToP2PInterface` never reach the
manager.

---

## 3. Internal ioctls

### 3.1 `ioctl_internal_req` and encoding

`IO80211PeerManager::ioctl_internal` (`0xffffff80022524e8`) takes a pointer to this 24 B structure
and executes it on the controller's workloop
(`*(*(manager+24)+1312)` → `vtable+3192` → `vtable+288(gated)`). Layout verified in
`ioctl_internal_gated` (`0xffffff800225238a`, reads `*(u32*)a2`, `*(u32*)(a2+4)`,
`*(void**)(a2+8)`, `*(u32*)(a2+16)`, `*(u8*)(a2+20)`, `*(u8*)(a2+21)`):

```c
struct ioctl_internal_req {
    uint32_t ioc;        // +0x00  IOC encoding (0x802869C8 = IOC_IN, size 40, 'i', number 200)
    uint32_t type;       // +0x04  apple80211 number (116, 117, ...)
    void    *payload;    // +0x08  caller's buffer, consumed in the same call
    uint32_t length;     // +0x10  payload length (0 in all manager uses)
    uint8_t  flags;      // +0x14  bit0: route through IO80211Glue::sendIOUCToWcl
    uint8_t  ifaceSel;   // +0x15  bit0: use the controller's interface
    uint16_t pad;        // +0x16
};                       // 24 B
```

- With `flags` at 0 (all the manager's ioctls) the path is
  `apple80211RouteInternal(ioc, type, iface, payload)`, with the AWDL interface `*(manager+24)+1376`
  (or `+1368` if the first is null).
- `length` is only used on the WCL route (`sendIOUCToWcl`); the manager always leaves 0 and the real size
  goes inside the payload.
- Return: 0 = success; anything else is an `errno` the manager translates with
  `(*(controller->vptr + 1920))(controller, ret)` in the logs. Except for the two cases noted in
  §3.13 (117 and 132), a failure is only logged and the manager continues.

Observed encoding: the low half of the qword is `0x802869C8` (direction `IOC_IN`, size 40,
group `'i'`, number 200) in all commands except the capability read, which uses
`0xC02869C9` (direction `IOC_INOUT`, number 201); the high half is the apple80211 number (`type`).

| Command (qword) | type | Name | Payload |
|---|---:|---|---|
| `0x75802869C8` | 117 | `setAWDL_SYNC_ENABLED` | 12 B |
| `0x155C02869C9` | 341 | `awdl_cap` read | 8 B (output) |
| `0x74802869C8` | 116 | `setAWDL_SYNC_PARAMS` | 20 B |
| `0x86802869C8` | 134 | `setAWDL_SYNC_STATE` | 24 B |
| `0x9B802869C8` | 155 | `setAWDL_ELECTION_PARAMS` | 36 B |
| `0x81802869C8` | 129 | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 400 B |
| `0x84802869C8` | 132 | `setAWDL_MASTER_CHANNEL` | 8 B |
| `0xCD802869C8` | 205 | `setAWDL_SECONDARY_MASTER_CHANNEL` | 8 B |
| `0xD0802869C8` | 208 | `setAWDL_AF_TX_MODE` | 8 B |
| `0x95802869C8` | 149 | UMI sending | 56 B |
| `0x97802869C8` | 151 | `setAWDL_SYNC_FRAME_TEMPLATE` | 16 B |
| `0xE5802869C8` | 229 | `setAWDL_AWDL_ADVERTISERS` | 44 B |
| `0xD3802869C8` | 211 | piggyback scan stop (event 43) | 8 B |

The `type` numbers are deduced from the high half of the qword. The names come from the table of
`AWDL_APPLE80211_DISPATCH.md` and from Broadcom's setters; 149 and 151 do not appear in that table and are
identified by their content (UMI and template), so their name stays as `?`.

### 3.2 117 — `setAWDL_SYNC_ENABLED` (`0xffffff80021dcf58`)

```c
struct awdl_sync_enabled {   // 12 B
    uint32_t reserved0;      // +0x00  = 0
    uint32_t reserved1;      // +0x04  = 0
    uint32_t enabled;        // +0x08  = 1 when enabling
};
```

Written in `enableAWDL` (`0xffffff80021dcf58`, `v50 = 0; v51 = 1; cmd = 0x75802869C8`). If the return
is non-zero: `ivars+414 = 0`, it calls `(*(vptr+432))(manager, 0)` (enable failure) and `enableAWDL`
returns the error. It is the **only** ioctl whose failure reverts the manager's state.

### 3.3 341 — `awdl_cap` read (`0xffffff80021dcf58`, start)

```c
struct awdl_cap {            // 8 B, output
    uint16_t version;        // +0x00
    uint16_t length;         // +0x02
    uint8_t  caps;           // +0x04
    uint8_t  pad[3];
};
```

Consumed bits (`v55`): `0x01` → `ivars+16576` `SEC_PAYLOAD`; `0x02` → `ivars+16577` `CCA_STATS`;
`0x08` → `ivars+16578` `PSF_DWELL_MI`; `0x10` → `ivars+16579` `PSF_DWELL_UMI`. The test of `0x01` is
done on the qword (`v55 & 0x100000000`), the others on the byte `+0x04`. If the ioctl fails it
logs `"ERROR: ioctl awdl_cap failed ret %x, awdl_cap 0x%X"` and **declares no
capability** (everything stays at 0): that is the safe answer for a driver without support.

### 3.4 116 — `setAWDL_SYNC_PARAMS` (`0xffffff80021ed024`)

```c
struct awdl_sync_params {      // 20 B
    uint32_t reserved;         // +0x00  = 0
    uint32_t aw_length;        // +0x04  ivars+62944  (getAvailabilityWindowLength)
    uint32_t aw_period;        // +0x08  ivars+62940  (getAvailabilityWindowPeriod)
    uint32_t ext_length;       // +0x0c  ivars+62948
    uint32_t sync_frame_period;// +0x10  argument a2 (µs? see note)
};
```

Written in `setSyncFramePeriod` (`0xffffff80021ed024`) with `cmd = 0x74802869C8`. Conditions:

- `a2 == 0` → assert (`"Error: Assert failure"`, line 25927) and return `-536870212`
  (`0xE00002BC`) without emitting.
- It is skipped if the period did not change and `force == 0`, or if `ivars+16943` (PSF dwell) and
  `ivars+16960 == a2` and `ivars+62952 == a2` (`"+++ Skipping Setting PSF period"`).
- Return 0 ⇒ `ivars+62952 = a2`. Return ≠ 0 ⇒ log `"fail to set PSF period to %u: %x"` and the ivar
  is **not** updated (that is why the manager retries on each `programChipWithNewMaster`).
- Invoked from `programChipWithNewMaster` (`0xffffff80021ec4b6`, three times) and from the
  enabling path; the value `a2` comes from `ivars+62952` (master indication period), never from
  `aw_length`/`aw_period`.

**The unit of `sync_frame_period` is not deduced from the code `?`**: the trace prints it as
`"period %u ms %u"`. A driver must treat it as the repetition period of the template of the 151.

### 3.5 134 — `setAWDL_SYNC_STATE` (`0xffffff80021ec4b6`, block `LABEL_3`)

```c
struct awdl_sync_state {        // 24 B
    uint32_t reserved;          // +0x00  = 0
    uint32_t valid;             // +0x04  = 1
    uint8_t  master[6];         // +0x08  ivars+37237 (elected parent/master)
    uint8_t  pad[2];            // +0x0e
    uint32_t op_mode;           // +0x10  1 = normal; 2 if ivars+37484 (forced master);
                                //        ivars+10307 (election fixed) forces 1
    uint8_t  flags;             // +0x14  0x20 if (*(*(ivars+480)+16)+12) == 1 (warmup)
    uint8_t  pad2[3];
};
```

Written in `programChipWithNewMaster` (`0xffffff80021ec4b6`) with `cmd = 0x86802869C8`. Return ≠ 0: log
`"error: %s  AWDL can't set op_mode for mast ..."` and it continues with the 155.

### 3.6 155 — `setAWDL_ELECTION_PARAMS` (`0xffffff80021ec4b6` and `0xffffff80021ed200`)

```c
struct awdl_election_params {   // 36 B
    uint32_t reserved0;         // +0x00  = 0
    uint32_t reserved1;         // +0x04  = 0
    uint64_t valid;             // +0x08  = 1
    uint8_t  op_mode;           // +0x10  ivars+37263
    uint8_t  pad;               // +0x11
    uint16_t election_id;       // +0x12  ivars+37259
    uint8_t  hop_count;         // +0x14  ivars+37247
    uint8_t  pad2;              // +0x15
    uint8_t  root_mac[6];       // +0x16  ivars+37231
    uint32_t root_metric;       // +0x1c  ivars+37251
    uint32_t self_metric;       // +0x20  ivars+37255
};
```

Written at `0xffffff80021ec4b6` (`v35`/`v36`) and at `0xffffff80021ed200` (`v7[2]`/`v7[3]`/`v7[4]`,
same pattern). `initFWElectionParams` is called when enabling; `programChipWithNewMaster` on each
master change. Return 0 ⇒ `ivars+728 = election_id`, `ivars+16056 = 0` and the 116 is re-issued with
`ivars+62952`. Return ≠ 0 ⇒ log `"AWDL can't send election params error %s"` and `ivars+728` is **not**
updated (which leaves the 1001 ms debounce of `ivars+10304` pending).

### 3.7 129 — synchronization channel sequence (`0xffffff80021cb69a`)

```c
struct awdl_chan_seq {              // 400 B
    uint32_t version;               // +0x00  = 1
    uint8_t  flags;                 // +0x04  ivars+43636, with SDB bits:
                                    //         |0x10 = SDB ON, |0x20 = SDB OFF, &0xCF = no SDB
    uint8_t  num_channels;          // +0x05  ivars+42820 (real bound of entries)
    uint8_t  unknown6;              // +0x06  ivars+42866
    uint8_t  unknown7;              // +0x07  ivars+42867 (sub-slots per window)
    uint8_t  unknown8;              // +0x08  ivars+42848
    uint8_t  unknown9;              // +0x09  ivars+42856
    uint8_t  pad[2];                // +0x0a
    struct apple80211_channel entry[32]; // +0x0c, 12 B each
};
```

- Each entry is copied from `ivars+42868 + 12*i` (12 B) in the same format as
  `struct apple80211_channel`: `+0x00 uint32_t version/flags`, `+0x04 uint32_t channel`,
  `+0x08 uint32_t flags`. The manager reads a channel of an entry in `monitorTimeline` as
  `*(u32*)(base + 12*i + 4)` and `setMyChanSeq` (`0xffffff8002235302`) writes 8 B of `+0x00` and 4 B of
  `+0x08`, which confirms those three fields.
- Bound: `num_channels` is not bounded in the copy loop (`while (i < *(u8*)(ivars+42820))`); with
  more than 32 entries the 400 B would overflow. A driver must assume `num_channels <= 32` (`?`
  on whether any path guarantees it).
- It is compared with the copy at `ivars+8` (`memcmp(..., 0x190)`) and, if `force == 0` and nothing changed and the
  SDB flag did not change, it is **not emitted**.
- Effect of the return: success ⇒ copies the 400 B to `ivars+8` and updates `ivars+63023`; error ⇒
  `recoverFromSetChanSeqFailure` (`0xffffff80021f3710`) retries with the previous sequence.

### 3.8 132 and 205 — social channels (`0xffffff80021d494a`)

```c
struct awdl_master_channel {   // 8 B
    uint32_t reserved;         // +0x00 = 0
    uint32_t channel;          // +0x04 ivars+17060 (132) / ivars+17072 (205)
};
```

Both are emitted in `programMasterChannels`: first `0x84802869C8` (132) and then `0xCD802869C8`
(205). Before, if `ivars+16940` or `ivars+16941` are active, the list is refreshed with
`updateFWSocialChanListWithPSFSuppressionInfo` (`0xffffff800220db4e`). If the 132 fails it logs
`"Unable to set master channel %u ret %d (%s)"` and **aborts** before the 205; if the 205 fails it
logs and returns the error.

### 3.9 208 — `setAWDL_AF_TX_MODE` (`0xffffff80021f7d06`)

```c
struct awdl_af_tx_mode {       // 8 B
    uint32_t reserved;         // +0x00 = 0
    uint32_t mode;             // +0x04 requested value; forced to 3 on 2.4 GHz without country
};
```

- It is skipped if the `(host, fw)` pair already matches `ivars+37144`/`ivars+37148`.
- If `mode == 5` (suppression) and the infra is on 2.4 GHz (byte `*(u8*)(ctrl+2496)` in 1..14 and
  `*(u32*)(ctrl+2500) & 0x2000 == 0`), or if `ivars+17044 == 0`, it sends `3` instead of `5`.
- Return 0 ⇒ it updates both ivars and, if the sent value was 0, calls `programMasterChannels`.

### 3.10 149 — UMI sending (`0xffffff8002213c0a`)

```c
struct awdl_umi {                    // 56 B
    uint32_t reserved0;              // +0x00  = 0
    uint8_t  dst[6];                 // +0x04  destination MAC (peer+88)
    uint8_t  pad0[10];               // +0x0a
    uint16_t option;                 // +0x14  6 = normal, 14 = "UMIOnDwell"
    uint8_t  self_bssid[6];          // +0x16  ivars+37204/+37208
    uint8_t  pad1[10];               // +0x1c..+0x27 = 0
    uint16_t template_len;           // +0x28  ivars+10248
    uint8_t  pad2[6];                // +0x2a
    uint64_t template;               // +0x30  ivars+9048 (MI template in ivars)
};                                    // total 0x38
```

Offsets recomputed over the decompilation of `sendUnicastMI`: the payload starts at `v36`
(`rbp-0x68`, `v35[1] = &v36`), the length is `LOWORD(v40)` (`rbp-0x40` → `+0x28`) and the pointer `v41`
(`rbp-0x38` → `+0x30`). A previous version of this note placed them at `+0x20`/`+0x28`.

Preconditions: `ivars+16096` bit 1 (AWDL connected), `>= 10 ms` since the last UMI to the peer
(`peer_ivars+1632`), `ivars+772` bit 0 (inside window) and `buildMasterIndicationTemplate` with
success. Option 14 is only used if `ivars+16943` and `ivars+16579` (PSF_DWELL_UMI) are active, the
peer was not discovered on 5 GHz and the last dwell was ≤ 109 ms ago. Return 0 ⇒ it updates
`ivars+42736` and `peer_ivars+1632`.

### 3.11 151 — sync frame template (`0xffffff80021dc5b2`)

```c
struct awdl_sync_frame_template {   // 16 B
    uint32_t reserved;              // +0x00  = 0 (¿version?) ?
    uint32_t length;                // +0x04  valid bytes of the template
    uint64_t template;              // +0x08  pointer to the 512 B buffer
};
```

The template is composed in a **512 B** buffer (`secure_memset(..., 0xAA, 0x200)`) and is emitted
only if `buildServiceParamTlv` returned `< 512`; the `length` is the accumulated total of TLVs.

Exact composition (each TLV is `{u8 type, u16 len, body}` with the length in the kernel's machine byte order,
**little-endian** on x86_64: `*(u16*)(tlv+1)`):

| Order | Type | Builder | Size | Content |
|---:|---:|---|---|---|
| 1 | 6 | `buildServiceParamTlv` `0xffffff80022118a6` | `12 + n` | `+0 type=6`, `+1 u16 len = n+9`, `+3 = 0`, `+4..+5 reserved`, `+6 u16 ivars+43656`, `+8 u32 ivars+43660` (bitmap of 32 services), `+12..` the `n` service bytes (`ivars+43664`, only the active bits) |
| 2 | 24 | `buildAWDLElectionParamsVer2` `0xffffff8002210a3c` | 43 | `+3 root_mac[6]`, `+9 master_mac[6]`, `+15 rootLFC`, `+19 hopCount`, `+23 rootMetric`, `+27 selfMetric`, `+31 electionId (u32 + 4 zeros)`, `+39 selfLFC (ivars+37560)` |
| 3 | `?` | `buildAwdlDataPathStateTlv` `?` | variable | datapath state, with the `SEC_PAYLOAD` capability |
| 4 | 7 | inline at `0xffffff80021dc5b2` | `3 + len` | `buildEnhDataRateTlv`, only if `*(ctrl+1564)` in 1..25 |
| 5 | `?` | `buildRangingTlv` `?` | variable | only if `ivars+42545` |
| 6 | `?` | `buildRealTimeGroupTlv` `?` | variable | only if `ivars+64418` or `ivars+65477` |
| 7 | 17 | inline (`*(v21-3) = 17`) | `3 + len` | `buildIEEE80211CntnrTlv` with 14 B of `ctrl+2401`, only if `*(ctrl+1568)` |
| 8 | `?` | `buildAWDLVersionTlv` `?` | variable | always |
| 9 | 20 | `buildSyncTreeTlv` `0xffffff80022112fe` | `3 + 6n` | `n = min(hopCount+1, 10)`, `len = 6n`, body copied from `ivars+37500`; only if `ivars+10308` |
| 10 | `?` | `buildNanSyncTlv` `?` | variable | always |
| 11 | `?` | `buildPrefChanTlv` `?` | variable | always |

**There are no timing fields in the template**: no builder writes into it the
window period/length ivars (`+62940/+62944`) nor the sync period (`+62952`). Timing is
contracted through ioctl 116 and events 41/42/43; the template only carries election parameters,
services, capabilities, sync tree and preferred channel. If the real firmware inserts the
`AwNumber`/`FwAwStartTime` into the sync frame, it does so outside this contract: a driver without firmware
must compose those fields itself from its own clock (`?` on the exact format the
air expects; the template of the 151 is only the "static" part of the TLVs).

### 3.12 229 — advertisers of the master (`0xffffff80021ec4b6`)

```c
struct awdl_advertisers {      // 44 B: schedule of the elected master, as it announced it
    uint16_t aw;               // +0x00  sn: number of the next AW        priv+106 (0x6A)
    uint16_t dc;               // +0x02  TU until the slot limit          priv+758 (0x2F6)
    uint16_t tx_delay;         // +0x04  phy_tx - target_tx, µs            priv+752 (0x2F0)
    uint16_t period;           // +0x06  period between AFs, TU (110)        priv+764 (0x2FC)
    uint16_t common_aw_len;    // +0x08  common AW length (16)         priv+772 (0x304)
    uint16_t ext_aw_len;       // +0x0a  constant 16 (required before)
    uint16_t aw_period;        // +0x0c  AW period, TU (16)            priv+762 (0x2FA)
    int8_t   rssi;             // +0x0e  peer+0x4A4, or peer+0x49C - 10, or -100 if both are 0 (?)
    uint8_t  pad0;             // +0x0f  0
    uint8_t  presence_mode;    // +0x10  AW per slot (4)                 priv+748 (0x2EC)
    uint8_t  pad1;             // +0x11  0
    uint8_t  distance;         // +0x12  distance to the root master         priv+575 (0x23F)
    uint8_t  pad2;             // +0x13  0 (dword +0x10 zeroed before)
    uint32_t mactime;          // +0x14  mactime of the master's last AF priv+912 (0x390), bswap
    uint32_t self_metric;      // +0x18  master's own metric (?)    priv+583 (0x247)
    uint32_t master_metric;    // +0x1c  root master's metric          priv+579 (0x243)
    uint8_t  root[6];          // +0x20  MAC of the root master              priv+559 (0x22F)
    uint8_t  master[6];        // +0x26  MAC of the master peer               peer+0x58
};
```

Read instruction by instruction between `0xffffff80021ec673` and `0xffffff80021ec72c`, with
`priv = *(peer+0x20)` (private block of the `IO80211AWDLPeer`) and the destination at `rbp+var_80`, which the log
`"aw %u dc %u tx_delay %u period %u ms %u (%u) ..."` prints in that order. Origin of each ivar:
- `+106`, `+748`, `+752`, `+758`, `+762`, `+764` and `+772` are written by `parseAwdlSyncParamsTlv`
  from the master's TLV 4 ([AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) §1.2 and §1.3).
- `+912` is written by `actionFrameReport` (`mov [rcx+390h], edx` at `0xffffff80021afaa4`) as
  `bswap` of the `mactime` (`+0x30`) of the 72 B header of the master's last AF, next to the
  `+0x10` channel of that header at `+1432`.
- `+559..+590` follow the order of the body of TLV 24 (master, sync, counter, distance, master's
  metric, own metric). `parseAwdlSyncTreeTLV` writes the distance at `+575`
  (`0xffffff80021b57ee`) and `parseAwdlElectionParamsV2` compares `+579` as the master's metric and
  `+559` as its MAC. The name of `+583` is an inference by position `?`.

**Use for the driver's timer.** The 229 arrives on each master change. With it, a driver
without firmware can align its slot clock to the master's the same way owly does (`Clock::update` in
`crates/owly-awdl/src/sync.rs`): the limit of the next slot is `dc` TU after the instant
the master's AF was received (`mactime`), and the AW starting there is the `aw`. The slot lasts
`presence_mode × aw_period` TU (4 × 16 = 64 TU). `tx_delay` corrects the delay between the planned
and the actual instant of that master transmission. While the master does not change, the driver must
keep adjusting the clock with the TLV 4 of its received PSF/MIFs, because the 229 is not re-sent.

Precondition: `memcmp(ivars+37231, ivars+37216, 6) != 0` (not being master) or `ivars+37484` bit 0
(forced master). Before emitting it does `lockDataPath`, looks up the master peer
(`findPeer(ivars+37237)`), and requires its MI to have length 16 (`LOWORD(...) != 16` ⇒
`"Error, master Ext length not valid"` and exit). Return ≠ 0 ⇒
`"FAIL: %s AWDL can't send AWDL_ADVERTISERS for parent ..."` and it continues with the 134.

### 3.13 Returns the manager checks (summary)

| Command | Does failure revert state? | Does it retry? |
|---|---|---|
| 117 | yes (`ivars+414 = 0`, `failToEnable`) | on the next `enableAWDL` |
| 341 | no (declares 0 capabilities) | only on the next enable |
| 116 | no (does not update `+62952`) | on each `programChipWithNewMaster` |
| 134 | no | on each master change |
| 155 | no (does not update `+728`) | on each master change |
| 129 | yes (`recoverFromSetChanSeqFailure`) | with the previous sequence |
| 132 | yes (does not emit the 205) | on each `programMasterChannels` |
| 205 | no | on each `programMasterChannels` |
| 208 | no | when the host/fw pair changes |
| 149 | no (does not update timestamps) | next UMI attempt |
| 151 | no | next `updateSyncFrameTemplate` |
| 229 | no | next `programChipWithNewMaster` |
| 211 | no | — |

---

## 4. TX pull: `dequeueTxPacket`, `getUnicastPackets`, `getMulticastPackets`

### 4.1 `TxPacketRequest` (96 B)

Layout reconstructed from `IO80211PeerManager::dequeueTxPacket` (`0xffffff8002250554`),
`IO80211PeerManager::getUnicastPackets` (`0xffffff800224ffdc`),
`IO80211PeerManager::getMulticastPackets` (`0xffffff800224fe4e`) and
`IO80211AWDLPeerManager::dequeueTxPacket` (`0xffffff800221675e`):

```c
struct TxPacketRequest {           // 96 B (0x60)
    uint16_t flags;                // +0x00  bit1 = ? (changes the AC order)
    uint16_t num_packets;          // +0x02  total requested / remaining
    uint16_t num_unicast;          // +0x04  total unicast requested / remaining
    uint16_t num_multicast;        // +0x06  total multicast requested / remaining
    uint16_t ac_count[8];          // +0x08..+0x17  remainder per queue/AC
    mbuf_t   packets[8];           // +0x18..+0x57  mbuf lists per queue
    uint16_t tail_status;          // +0x58  base: bitmap of active queues
                                   //   (`ctrl+2504`); AWDL manager: 0
    uint8_t  pad[6];               // +0x5a..+0x5f
};
```

Verified details, which are the ones that matter to the driver:

- **Pointers**: the base implementation walks `i = 0..7` reading `*(mbuf**)(req + 24 + 8*i)` and
writing the count to `*(u16*)(req + 8 + 2*i)`. The AWDL variant walks only `i = 0..3`
(pointers `+0x18, +0x20, +0x28, +0x30`, counts `+8, +10, +12, +14`), and that is why `packets[4..7]`
goes unused in AWDL.
- **Counts**: `num_packets` is the total; `num_unicast`/`num_multicast` are decremented on each
extraction; `ac_count[i]` is the remainder per queue. `getUnicastPackets` extracts
`min(ac_count[ac], num_unicast, num_packets)` and discounts from all three; `getMulticastPackets` extracts
`min(num_packets, num_multicast)` and discounts from both.
- **Destination queue**: the mbufs are chained with `mbuf_setnextpkt` at the end of
`req->packets[i]`; the head is never replaced when packets already exist.
- **`+0x58`**: `*(u16*)(req+88)`. The base writes there the bitmap of active queues
  (`*(u16*)(ctrl+2504)`); the AWDL manager writes 0 (`a2[44] = 0`) when there is no window.
- The caller (`IO80211VirtualInterface::dequeueTxPackets`) walks the 8 pointers
`req[3..10]` to account in `IO80211InterfaceMonitor::recordOutputPacket`, and if there is no peer
manager it zeroes the 96 B.

### 4.2 Who calls and when

```
Skywalk (TX of the interface) → IO80211VirtualInterface::dequeueTxPackets(req)   [0xffffff80023313b2]
        └─ (*(pm->vptr + 0x2A8))(pm, req)  = IO80211AWDLPeerManager::dequeueTxPacket  [0xffffff800221675e]
                ├─ if applicable: IO80211PeerManager::dequeueTxPacket(pm, req)  [0xffffff8002250554]
                └─ and per AC: (*(pm->vptr + 0x170))(pm, req, ac)  = getUnicastPackets [0xffffff8002216a4e]
                             (*(pm->vptr + 0x168))(pm, req)      = getMulticastPackets [0xffffff8002216a2e]
```

`IO80211AWDLPeerManager::dequeueTxPacket` (`0xffffff800221675e`) imposes:

1. Base state asserts: `*(pm+24)+1328 != 0` (interface), `+1336 == 0`, `+1344 != 0` (queue). If
   they fail, log `"Error: Assert failure"` and return 0 (no packets).
2. It zeroes the four pointers `+24..+48`.
3. `lockDataPath` (`*(*(pm+24)+120)` → `vtable+280`); if it fails, log
   `"lockDataPath failed"` and return 0.
4. **Window decision**, in this order:
   - If `*(u32*)ivars != 0` (AWDL enabled) and `*(u32*)(ivars+408) >= 4` (already served 4
     requests in the current window): it sets `*(u16*)(req+6) = 0`, i.e. it **cancels the multicast** of
     the request before extracting.
   - If `*(u32*)ivars == 0` (AWDL disabled) and (`*(u32*)(ivars+10468) != 5` and
     `*(u8*)(ivars+17032) != 0`, data mode): it sets `+88 = 0`, leaves the request empty and exits without
     extracting.
   - In the remaining cases it does not touch the request.
5. Extraction: `IO80211PeerManager::dequeueTxPacket` (which calls `getMulticastPackets` if
   `flags & 2`, and `getUnicastPackets` with the AC order `{3,2,3,1}` if `flags & 2 == 0` and
   `{0,2,0,1}` if active), and afterward counts the mbufs of each one of the 4 queues in
   `ac_count[i]`.
6. `unlockDataPath` and trace `"[t:%u mU:%u mM:%u pktCnt %u %u %u %u] numpeers %u reqTx 0x%x txav%u data mode %u dpopen %u got %d pkts"`.

`getUnicastPackets`/`getMulticastPackets` of the AWDL manager only wrap the base ones and add the
result to counters (`ivars+16360` and `ivars+16368`).

### 4.3 Driver obligations

- Serve `dequeueTxPacket` (or let the caller do it) only with the datapath open: the manager
  carries nothing, it only fills `TxPacketRequest` with mbufs already formatted by
  `formatOutputPacket` (cell 40). The transmission instant within the window is decided by the
  driver.
- Do not retain the mbufs: ownership passes to the caller as soon as they are chained.
- Call with `req` at zero before each request: the manager only clears the four pointers it uses
  and the counts it decrements; the rest of the state is provided by the caller.

---

## 5. Minimums a driver without AWDL firmware must meet

1. **Inject AF** through `bpfTapInput(iface, buf, 12, hdr, 72)` with an `IO80211Buffer` from a
   valid pool and an `apple80211_act_frm_hdr` of exactly 72 B, source MAC at `+0` and RSSI in dBm
   (`int32_t`, host order) at `+0x2c`; below the edge threshold (−70 default) the AF is
   discarded. Never reuse the buffer afterward: the manager returns it to its pool (`+0x118`).
2. **Publish events 41/42/43** with the payloads of §2: 36 B for the 41 (`AwNumber` at `+0`,
   master at `+8`, top master at `+0x0e`, `FwAwStartTime` big-endian at `+0x14`, channel at `+0x1c`),
   4 B for the 42 (`sn` of the window), 40 B for the 43 (CCA flag at `+0x10`, base at `+0x18`,
   counters at `+0x1c/+0x20/+0x24`). Without the 41 the manager never enters the window
   (`ivars+772` stays at 0) and `sendUnicastMI` rejects everything.
3. **Accept ioctls 116/117/129/132/134/149/151/155/205/208/229** with the layouts of §3 and
   return 0. The only one whose failure reverts the state is the 117; the 341 must return 0 capabilities
   if `SEC_PAYLOAD`, `CCA_STATS` and `PSF_DWELL_*` are not implemented. 143 must return 0 and then
   post event 51 (§2.6).
4. **Repeat the template of the 151** every `sync_frame_period` (116) aligned with the shared
   timeline: it is the only obligation the manager delegates completely and has no
   feedback on.
5. **Serve the TX pull** during the windows (§4), without retaining mbufs.

## 6. Corrections to `AWDL_PEERMANAGER_SOFTWARE.md`

- Event 47 is **not** translated into `cleanUpForPeerRemovedFromNDP`: the manager delegates it to the
  base class and the latter does not handle it either; it ends up published to userspace. That cleanup is triggered from
  `IO80211AWDLPeer::setPeerNotReachable`.
- The payload of ioctl 116 is `{0, aw_length, aw_period, ext_length, sync_frame_period}`, not
  `{period, length, ?, period}`.
- The payload of ioctl 155 has `op_mode` at `+0x10`, not at `+0`; the first 16 B are reserved
  plus a `u64` at 1.
- The template of the 151 contains no timing fields; the window period and length
  travel only through the 116 and the events.
- The `len` of TLV 20 (sync tree) is `6n`; the total returned size is `3 + 6n`.
- The return of `reportDataPathEvents` means "let it pass to userspace" (≠0), not "handled".
