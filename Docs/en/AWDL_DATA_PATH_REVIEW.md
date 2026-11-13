# Review of the AWDL data path: `AWDLInterface` (RX/TX) and `AWDLTx` (radio)

Independent defect hunt in the new data path: `deliverData` (RX 802.11→Ethernet),
`txData`/`queueData`/`sendData`/`flushRadioTx`/`txTick`/`setRadioTx`/`free` (TX data and its ring) in
`Source/AirLayer/AWDLInterface.cpp`/`.h` (HEAD: `5b686a8`, `5630d49`, `f2969a4`, `9dc71a6`) and
`rtl8814native_radio_tx_80211`/`rtl8814native_radio_awdl_port`/`rtl8814native_radio_set_channel` in
`Source/Driver/AWDLTx.c` (`42c04d3`, `0eb77c2`, `8726bd4`). Method: line-by-line reading against the
reference STA path (`Source/AirLayer/RTLAirLayer.cpp`, `txAction`/`rxFrames`), the owly on-air
format (`~/Desktop/AWDLLab/refs/owly`, `frame.rs`), the docs `AWDL_RX_DATA_TO_SKYWALK.md`,
`AWDL_DATA_ENCAP.md`, `AWDL_STAGE1_IFUP_PATH.md`, `AWDL_STAGE3_EVENT_EMITTERS.md` and the work spec
(`AWDL_TX_DATOS_SPEC.md`), and the vendor tree (`rtw_xmit.c`, `rtw_mlme_ext.c`, `wifi.h`, `rtw_xmit.h`,
`RTL8814Native.cpp`). Line = working HEAD. What was only checked statically (without IDA or
hardware) carries `?`.

## 1. Findings summary

| # | Severity | Where | Defect |
|---|---|---|---|
| R1 | High | `AWDLInterface.cpp:736` | Use after free of `_rxCompQ` (and of `_rxPool` in the variant with pool teardown) when `stop()` runs while the RX drain is distributing data |
| R2 | High (`?`) | `AWDLInterface.cpp:920` | `thread_call_cancel_wait` wait cycle (tick) against the controller gate that `postMessage` needs; deadlock if `disable`/`stop` run in the thread that serves that gate |
| R3 | Medium | `AWDLInterface.cpp:1257` | `_txDataOff` stays stuck at `true` forever: neither `enable` nor `setRadioTx(true)` re-arm it |
| R4 | Medium | `AWDLInterface.cpp:692` | `deliverData` skips the `inputPacket` pre-hook (+0xB50): no peer filter nor ioctl 415 retention |
| R5 | Medium/Low | `AWDLInterface.cpp:1186-1191` | 12-bit AWDL seq with a non-monotonic seed: jump backward after seat eviction; contradicts the project's own spec and owly (u16) |
| R6 | Low | `AWDLInterface.cpp:333` | `preQueuePacketCheckQuietMode` conditioned on `_txOn`; Broadcom calls it for every enqueued packet |
| R7 | Low | `AWDLInterface.cpp:563` | `_rxHooked` read outside `_rxLock` in the drain (benign race, but it invalidates the R1 gate) |
| R8 | Low | `AWDLInterface.cpp:369,379` | `thread_call_free` silently failing if the last release arrives from the drain itself |
| R9 | Low | `AWDLTx.c:68-69` | `pmlmeext->mgnt_seq` incremented without a lock from two threads |
| R10 | Low | `AWDLTx.c:111-117` | RMW of `MSR` (port 1) without serialization against the MLME `rtw_hal_set_msr` |
| R11 | Informational | `AWDLInterface.cpp:652` | 2048 B stack copy (`deliverData`) plus `injectFrame`: ~3 KB of stack per data frame |
| R12 | `?` verify | `AWDLInterface.cpp:680-687` | A-MSDU subframe length convention: the parser follows owly (without the 14 B header), IEEE 802.11 defines length = MSDU + 14 |

## 2. Findings with scenario and fix

### R1 (High) — Use after free of the `awdl0` RX queue during teardown

`deliverData` enqueues into `_rxCompQ` without retention (`AWDLInterface.cpp:736`), reading the
object pointer. The queue lives in `removeQueues` (`:291-307`), which `disable()`/`release()`s and
nulls it inside `teardownDatapath` (`:348-355`) from `stop()` (`:357-363`).

The drain (`rxDrainEntry`, `:551-569`) runs on a separate thread call, not on the work queue: it
retains the object (`:545`), but nothing serializes its work with `removeQueues`. The only guard is
`if (me->_rxHooked)` (`:563`), read without a lock and moreover of check-then-act type: a frame already
taken off the chain when the hook deactivates enters `injectFrame`→`deliverData` and may be
between `rtl_pool_alloc` and `rtl_rxcomp_enqueue` when `removeQueues` does `q->release()`.

Scenario: AirDrop active (RX data frames flowing at ~10-50 per window) and `ifconfig awdl0 down`
followed by interface destruction (the chain `detachVirtualInterface→terminate→stop` is synchronous,
`AWDL_PEERMANAGER_TEARDOWN.md`). `setRadioRx(false)` deactivates the hook and drains the pending
queue (`flushRadioRx`), but the drain already took its chain: `deliverData` enqueues onto the freed
queue → kernel panic. The window is small (tens of µs per frame) but repeats with every frame in
flight.

The pool `_rxPool` is not touched by `removeQueues` (only the queues are), so the lethal window is
that of `_rxCompQ`; `free()` (`:392-394`) would free again the pointers already nulled by
`removeQueues` (that one is covered with `OSSafeReleaseNULL` over the field to `nullptr`).

Fix (the smallest one): retain the queue around the enqueue, with the same lock the teardown
already takes. For example: in `deliverData`, under `_rxLock`, copy `void *q = _rxCompQ;` plus
`((OSObject *)q)->retain()` if it is not `nullptr` and `_rxHooked` is still `true`; release after
the enqueue. In `removeQueues`, take `_rxLock` before `disable()`+`release()` and null the field
inside. That way enqueueing onto an already-freed queue is impossible and the drain sees either the
old state or `nullptr`. A cleaner alternative: move the data distribution to the work queue with
`runAction` (the pattern already used by `bpfTapAction`, `:631-633`), which serializes with
`removeQueues` by construction; the cost is one less direct copy and passing the `mbuf` as an
action argument.

### R2 (High, `?`) — Cycle `thread_call_cancel_wait` ↔ `postMessage` on the controller gate

`setRadioTx(false)` (`:920`) calls `thread_call_cancel_wait(_txTick)` and waits for the tick to
finish. The tick publishes events 41/43 with `postMessage` outside `_txLock` (`:1032`, `:1036`), and per
`AWDL_STAGE3_EVENT_EMITTERS.md` §1.2 that virtual call enters `reportDataPathEvents` and passes through the
controller gate (`IONetworkController::executeCommand`), which in its non-embedded form **waits**
for the gate to become free.

The cycle closes if whoever calls `setRadioTx(false)` is in turn the thread that serves that gate, or a
thread whose progress depends on the gate holder:

- `disable(unsigned)` arrives from `ioctlHandler` (`SIOCSIFFLAGS` → `adjustInterfaceEnable`,
  `AWDL_STAGE1_IFUP_PATH.md` §4); if the Skywalk netif ioctl runs serialized on the work loop of the
  interface — which is the same work loop where the controller gate lives (`?`, not verified in IDA) —,
  `cancel_wait` blocks the thread serving the gate while the tick waits for the gate to publish
  the event 43/41 that lands at the instant of the `down`.
- `stop()` arrives from the synchronous `terminate→stop` teardown chain; if that chain runs with the
  gate taken (`?`), same cycle with the whole radio blocked (every later ioctl hangs).

Concrete scenario: a window opening/closing every 4 AWs (~65 ms); an `ifconfig awdl0 down` that
lands while the tick is inside `postMessage` with the gate held by another ioctl → the two threads
wait for each other and `en2`/`awdl0` are left without gate attention.

Fix: never wait from `setRadioTx`. The pattern already exists in the RX drain: `txTickEntry`
retains the object on entry and releases on exit (`retain` before `thread_call_enter`, `release` at
the end of `txTick`), and `setRadioTx(false)` just does `_txOn = false` under `_txLock` +
`thread_call_cancel(_txTick)` (no wait). `free()` no longer needs the invariant «stop waited for
the tick»: per-invocation retention guarantees the tick does not touch the object after the last
`release`, and `thread_call_free` may fail silently just as today (R8). With that the `down` never
blocks the gate server.

### R3 (Medium) — `_txDataOff` without re-arm

`sendData` sets `_txDataOff = true` on `kIOReturnBadArgument` (`:1254-1259`) and nothing sets it back
to `false`: neither `setRadioTx(true)` (the `on && !was` transition of `:897-904` restarts the clock but not this
flag), nor `enable`, nor a hot driver change. After a single rejection (pre-AWDL-data-TX driver,
or a frame the driver rejects by length/`frame[1]`), all TX data of `awdl0` is
dead until the kext is reloaded, even though the cause disappears.

Fix: in the turn-on transition (`setRadioTx`, block `if (on && !was)`), add
`_txDataOff = false;` under `_txLock`. The old-driver case retries once per `enable`, which is
the right cost.

### R4 (Medium) — `deliverData` does not call `inputPacket` (+0xB50)

The comment (`:643-644`) declares it: «The peer filter of IO80211VirtualInterface::inputPacket (§6)
is not applied». But the documented contract (`AWDL_RX_DATA_TO_SKYWALK.md` §6 and §8.6;
`AWDL_DATA_ENCAP.md` §7.5) says Broadcom calls slot +0xB50 before enqueueing and that **the family
filters nothing the driver enqueues without that call**: without it, frames from non-paired nodes
reach the stack, the special cases `33:33:00:00:xx:xx` / `01:00:5E:00:xx:xx` are not applied, and
the packets held waiting for ioctl 415 (traffic registration) do not exist — wifip2pd sees data
before the registered service exists.

Scenario: a neighbor with AWDL in sight (not paired) sends data frames to the group or to the MAC of
`awdl0`: the stack processes them as if the peer existed; and the privacy window the 415
implements disappears.

Fix: after `rtl_netpkt_setLinkHdrLen` (`:730`) and before adding to `pkts[]`, build a 12 B
RX tag (bit 0 = multicast, `+6..+11` = SA) and call slot +0xB50 of the interface with
`(pkt, tag, frame, &flag, 0)` like Broadcom at `0xffffff800154a65c`; return 0 → `rtl_pool_free`
and `_rxDataNotUs++`. It is a virtual call of the interface itself, with no new dependencies. If
keeping the omission for the first phase is preferred, record it in `AWDL_TX_DATOS_SPEC.md`/the
index as a decision and not as an implicit pending item.

### R5 (Medium/Low) — AWDL sequence: 12 bits and a backward seed

`queueData` (`:1186-1191`): `seq = e->seq & 0xFFF` and the seed of a recycled seat is
`(uint16_t)(now >> 4)` (`:1187`), truncated to 12 bits when used. Two deviations:

1. owly keeps a u16 counter per node that wraps at 65536 (`frame.rs`, `awdl_seq.wrapping_add`),
   and the project's own spec (`AWDL_TX_DATOS_SPEC.md` §3.2) said «first frame 0» with wrapping at `0x0FFF`;
   commit `f2969a4` changed the seed to non-null without updating the spec.
2. After seat eviction (16-destination LRU) and reappearance of the destination, the sequence
   **jumps backward** to a value derived from the clock. If the Apple receiver uses the field to
   deduplicate/reorder (semantics marked `?` in `AWDL_DATA_ENCAP.md` §9.2), a backward step can
   cost transient drops; with wrapping at 4096 the cycle is additionally 16× shorter than owly's.

Minimal fix: a full monotonic u16 counter per seat (`e->seq = (uint16_t)(e->seq + 1)` without the
mask on use, keeping the seed only for the just-created case) or, if the high nibble is kept at 0
by decision, at least seed with the full `(uint16_t)(awdlNowUs() >> 4)` and wrap at
65536. Requires confirming with a capture what the receiver expects (`?` already noted in the spec).

### R6 (Low) — Quiet-mode notification conditioned on `_txOn`

`txData` (`:333`): `void *pm = me->_txOn && len >= 14 ? ...`. With `rtlawdl_tx=0` (or before
`enable`), the send queue keeps delivering stack packets and `queueData` drops them, but
`preQueuePacketCheckQuietMode` is not called: the manager stays in quiet mode and its data filtering
stays misaligned with what Broadcom does per enqueued packet (`AWDL_TX_HOOKS_POR_PAQUETE.md` §2,
d97c680). Fix: drop `_txOn &&` from the condition (the call is cheap and does not touch the radio).

### R7 (Low) — `_rxHooked` without a lock in the drain

`rxDrainEntry` reads `me->_rxHooked` outside `_rxLock` (`:563`). A benign race by itself (worst
case: releasing an injectable frame), but it is the gate R1 turns into a UAF. Closed by the R1
fix (read and retain under `_rxLock`).

### R8 (Low) — `thread_call_free` can fail in `free()`

`free()` (`:369`, `:379`) calls `thread_call_free` ignoring the return. If the last release arrives
from `rxDrainEntry` (the normal unload case with traffic), `free()` runs **on the drain's own
thread** with the thread call still marked «running»: `thread_call_free` returns `false` and the
structure leaks (once, at unload). With the R2 fix (per-invocation retention also in the tick) the
same pattern applies to `_txTick`. Not frequent nor corrupting; record it and, if it should be
closed, use `thread_call_cancel` + accept the leak only if it returns `false`, or defer the
`thread_call_free` to a deferred work item.

### R9 (Low) — `mgnt_seq` shared without a lock

`rtl8814native_radio_tx_80211` does `SetSeqNum` + `pmlmeext->mgnt_seq++` (`AWDLTx.c:68-69`) from the
AWDL tick (thread call) while the STA command threads do the same. Without a lock, two frames can
go out with the same number. Today it is harmless: the descriptor renumbers the non-QoS ones with
`HWSEQ_EN` (comment `:71-74`), and both paths emit non-QoS. It stops being so if QoS is ever sent
here. Fix: increment with `atomic_fetch_add` or with the lock that already protects the command
queue.

### R10 (Low) — Unserialized RMW of the `MSR` of port 1

`rtl8814native_radio_awdl_port` does read-modify-write of `MSR` bits 3:2 (`AWDLTx.c:111-117`)
without the serialization the MLME `rtw_hal_set_msr` uses. A concurrent STA mode change (link
up/down) can stomp the port 1 bits or be stomped by them: the effect is the same as already
documented in the parking review (loss of ACKs until the next `enable`/`disable`). With concurrent
off the windows are narrow. Fix: reuse the same lock the MLME uses for `MSR`, or
re-read-verify after writing and repeat once.

### R11 (Informational) — Stack pressure in the RX path

`deliverData` copies the frame to `uint8_t f[2048]` on the stack (`:652`) and adds `sub[16]` (512 B) and
`pkts[16]` (128 B); the chain `rxDrainEntry→injectFrame→deliverData` adds ~3 KB on the thread call
thread. With the 16 KB kernel stacks of macOS x86_64 there is margin; with 8 KB stacks (other
platforms/`?`) it would be tight. If ported, the per-`runAction` variant of R1 also moves the copy
onto the work queue stack.

### R12 (`?`) — A-MSDU subframe length convention

The parser (`:680-688`) follows owly: the BE16 length of each subframe counts **only the body** (SNAP +
AWDL head + payload, `16 + payload`; `frame.rs:344`), and the advance is `14 + sl + pad`
(`frame.rs:246`). IEEE 802.11 defines that length **including** the 14 bytes of the subframe
(`MSDU + 14`). If an iPhone emits with the IEEE convention, each subframe is delivered with 14
extra bytes and the walk desynchronizes from the second subframe (single-MSDU non-QoS frames, the
majority of early traffic, are unaffected). owly may have normalized to its own
convention: the primary source is an iPhone capture. Proposed verification: replay via
`deliverData` of a captured A-MSDU frame (the `AWDLLab` extractor already rebuilds records;
a data capture is needed, not only AF) or a test with the ESP32 peer emitting both
conventions. A cheap defensive fix meanwhile: if the SNAP at +14 does not match but the one at
`+14+sl-14` does, reinterpret with `sl -= 14` and continue the walk with the detected convention.

## 3. Checks that came out clean

To avoid repeating the work in the next pass:

- **TX on-air format** (`queueData` `:1199-1216`): FC `0x08` non-QoS like owly `frame.rs:313`
  (`data_frame`), A1=DA `+4`, A2=SA `+10`, A3=AWDL BSSID `+16`, SNAP `AA AA 03 00 17 F2 08 00`,
  `03 04` at `+8/+9`, seq u16 **LE** at `+10` of the body, `00 00`, ethertype **BE** taken from
  `eth[12..13]`, payload `+16`. The 802.11 frame goes without QoS and without A-MSDU (like owly). The
  hardware renumbers the 802.11 sequence (`AWDLTx.c:71-74`).
- **Ring arithmetic** (`:1155-1217`): `len ≤ 1442` (14 + MTU 1428), max frame `24+16+1428 =
  1468` = exact size of `AwdlTxFrame.frame`; `sizeof(_txRing->frame)` does not dereference;
  drop-oldest correct; the re-enqueue of `sendData` after `Busy` (`:1262-1272`) does not stomp live
  frames (the busy window after backing the head does not reach the last enqueued: `head-1` is outside
  `[head, head+count)` with `count ≤ 31`).
- **Copies with `offsetof`** (`:1236`, `:1266`): `6 + len ≤ 1474 ≤ sizeof(AwdlTxFrame)` (1476 with
  padding). Safe.
- **Frame age** (`:1240`): u32 arithmetic with correct wrapping (difference modulo 2³², age
  1.2 s ≪ wrap period of ~71.6 min).
- **RX `deliverData` limits** (`:652-688`): the frame arrives ≤ 2048 from the hook (`:511`); `len-4 ≤ 2044`;
  the A-MSDU walk bounds `off+14+sl ≤ len` before cutting and the SNAP `memcmp` after requiring
  `sl ≥ 16` (correct short-circuit); `elen ≤ 2044` fits in the pool packet (2048) and is re-verified
  against `getBufferSize` (`:714`); no reads outside the stack buffer under any short-circuit
  order (the only graze, `f[22]` with `len < 24` at `:667`, is covered by the final `len < hdr`).
- **Skywalk contracts**: `prepare` before touching the buffer (`:713`, with panics 0.15/0.16
  documented); `enqueuePackets` with `n < capacity` (chunks of 64 < 128 in `txData` `:339-341`;
  `n ≤ 16` < 128 in `deliverData` `:736`); packets completed exactly once (or returned to
  the pool on every error: `:342-343`, `:715-717`, `:725-728`, `:738-740`); the action returns
  `count` (IFUP §5) and only returns 0 in the null `_txCompQ` guard, with the queues already dead.
- **TX locks**: the radio is only called with `_txLock` released (`sendAf` `:1135`, `sendData`
  `:1245`); `postMessage` outside the lock (`:1030-1039`); `buildAf` reads `_tpl`/clock under
  the lock (`:1292-1296`), inheritance of fix H1; `servedMask` and the 129 sequence under
  the lock; tick re-arm invariant verified (`cancel_wait` on every transition to off
  guarantees the `thread_call_enter` of a later enable really schedules).
- **Driver limits**: `len ≤ 1536-48 = 1488` (`MAX_XMIT_EXTBUF_SZ - TXDESC_OFFSET`,
  `rtw_xmit.h:89/200` and `rtl8814bu_hal.h:27`) ≥ 1468 of the ring and ≥ 1024 of the AFs; the
  half-pool gate (`AWDLTx.c:49`) consults `free_xframe_ext_cnt`, which is exactly the counter
  `alloc_mgtxmitframe → rtw_alloc_xmitframe_ext` consumes (`rtw_mlme_ext.c:7496-7527`,
  `rtw_xmit.c:3608-3618`); the ext-buffer failure is covered by the `NULL` of
  `_alloc_mgtxmitframe`. The mask `frame[1] & 0x43` rejects To/From DS, more-frag and protected; the
  BMC bit drops multicast retries (`:75-77`); OFDM rate 6/12 on the AWDL BSSID even when the
  STA is on 2.4 GHz (`:60-63`).
- **`setRadioTx`**: `cancel_wait` before releasing port 1 (`:920-923`); `flushRadioTx` at the end;
  the clock restarts only on the transition; `_port1On` stays `true` only if the port answered,
  and `queueData` requires port 1 for unicast (`:1169-1173`).
- **RX hook/mbuf queue**: limit 64, copy with `mbuf_copyback` before enqueueing, kick only on the
  first element, object retention while the drain lives (`:545-548`, `:568`); no mbuf leaks on any
  return of `radioRxHook`/`rxDrainEntry`/`injectFrame`/`deliverData`.
- **Park and sequence**: `parkRadio` and the 129 read `_seq` under `_txLock`; the definitive
  failure only with `Unsupported`/`BadArgument` (`9dc71a6`); `RTW_CMDF_CHBW_IDLE_ONLY` re-checks the
  MLME on the command thread (`8726bd4`).

## 4. Pending external verification

1. R2: confirm in IDA on which thread/lock `ioctlHandler` (SIOCSIFFLAGS) runs and the synchronous
   `terminate→stop` chain of the role 6 interface, and whether that work loop is the one serving
   the controller gate `postMessage` waits on (`AWDL_STAGE3_EVENT_EMITTERS.md` §1.2).
2. R12: A-MSDU length convention of a real iPhone (data capture).
3. R5: semantics of the seq field at the Apple receiver (already `?` in `AWDL_DATA_ENCAP.md` §9.2).
4. R11: stack size of the thread call thread on the target boot (16 KB assumed).

## 5. Status after verification

The lines of the previous sections are those of the pre-fix tree.

| # | Status | Detail |
|---|---|---|
| R1 | Fixed | `stop()` waits with `thread_call_cancel_wait` on the RX drain (and the tick) after `setRadioRx(false)` and before `teardownDatapath`; a cancelled pending call drops its reference. `stop` runs on the IOKit termination thread without the controller gate (`AWDL_PEERMANAGER_TEARDOWN.md`, step 6), so the wait does not cross `postMessage`. |
| R2 | Fixed | IDA (25G241): `IOSkywalkNetworkBSDClient::ioctlHandler` (`0xffffff8002affce6`) re-enters its own gate if not in it and calls `adjustInterfaceEnable` (`0xffffff8002b006ee`, sites `0xffffff8002affed5` and `0xffffff8002afffcf`) with that gate taken, between the sleep and wake calls of `this+0x90` (slots `+0x1E0` and `+0x1E8`). Whether that gate is the controller's remains unchecked; the fix does not depend on it: `setRadioTx(false)` no longer waits (only `thread_call_cancel` under `_txLock`), each pending or in-flight tick call retains the object (`armTick`, like the RX drain) and `_tickBusy` prevents two ticks at once when an `enter` arrives with a tick still running; that tick re-arms only if it finds `_txOn`. |
| R3 | Fixed | `setRadioTx(true)` sets `_txDataOff = false` on the turn-on transition. |
| R4 | Deferred | Deliberate omission of the first phase, noted in the `deliverData` comment and in `AWDL_PRUEBA_CON_IPHONE.md` §7: without the `+0xB50` pre-hook more traffic reaches the stack, not less, and the ioctl 415 retention does not exist. Revisited after the test with a peer. |
| R5 | Not a defect | Broadcom encodes `seq = (counter & 0xFFF) \| (AC << 12)` with a counter per destination (`AWDL_DATA_ENCAP.md` §7, item 3); with AC 0 it matches the code. owly writes the u16 unmasked, but the reference is the Apple driver. The clock-based seed after eviction is kept. |
| R6 | Kept | With TX off there are no windows or sending: taking the manager out of quiet mode changes nothing on the air. |
| R7 | Closed with R1 | The wait of `stop()` makes the unlocked read ineffective. |
| R8 | No change | Recorded; with R2 the tick follows the same reference pattern as the drain. |
| R9, R10 | No change | Harmless today via `HWSEQ_EN` and with concurrent mode off. |
| R11 | No change | Informational. |
| R12 | Not a defect | IEEE 802.11 defines the A-MSDU subframe length as the MSDU length, without the 14 B of DA, SA and length; the parser and owly follow that convention. |
