# AWDL window timer for a radio without AWDL firmware

Specification of the slot clock the RTL8814AU layer has to carry on the host to do what Broadcom's
firmware does: follow the master's schedule, emit events 41/42/43 and know which channel to be on
in each slot.

It contains no new kernelcache data: it gathers into a single contract what is already verified in
[AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) (§2 events, §3.12 ioctl 229),
[AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) (ioctl 129) and
[AWDL_TLV_BODIES.md](AWDL_TLV_BODIES.md) (TLV 4). It also takes owly's clock model
(`crates/owly-awdl/src/sync.rs`, MIT), which keeps that schedule within 3 TU of the master with
host timers. Anything untested on hardware carries `?`.

## 1. Units

| Quantity | Value | Source |
|---|---|---|
| TU | 1024 µs | 802.11 |
| AW | 16 TU = 16 384 µs | TLV 4 `+8` = 16 in all captures |
| Slot | 4 AW = 64 TU = 65 536 µs | TLV 4 `+30` (presence mode) = 4 |
| Sequence | 16 slots = 1 048 576 µs = 2^20 µs | TLV 18 |
| `AwNumber` | AW count, `u16` with overflow | event 41 `+0x00`; TLV 4 `+32` |
| `FwAwStartTime` | µs of the AW start, `u32` big-endian | event 41 `+0x14` |

`monitorTimeline` expects that, between two events 41, `FwAwStartTime` advances `(ΔAwNumber) << 14`,
i.e. 16 384 µs per AW. This confirms the firmware unit is the µs. It tolerates up to 5120 µs of
error before logging «AW start time inaccuracy», and only logs
([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.3).

## 2. Clock state

Two values suffice, as in owly:

- `base_us`: host instant (in µs, of the monotonic clock) at which AW `base_aw` started.
- `base_aw`: number of that AW.

From there, for an instant `t`:

```
aw(t)        = base_aw + floor((t - base_us) / 16384)          (mod 2^16)
slot(t)      = (aw(t) >> 2) & 0xF
slot_start   = base_us + 65536 * floor((t - base_us) / 65536)  (if base_aw is a multiple of 4)
next_slot_tu = 64 - floor(((t - base_us) / 1024) mod 64)       (what goes in TLV 4 +4)
```

The slot index matches the one the manager computes:
`min(15, (AwNumber % ((step+1)·n)) / (step+1))`, with `step+1 = 4` and `n = 16`
([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2).

## 3. Where the schedule comes from

### 3.1 The layer is the master

This is the `peers:0` case (`runElection` elects itself; hardware result in
[AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)). No 229 arrives (its
precondition is not being master) and the clock runs free: `base_us` = enable instant
(ioctl 117), `base_aw` = 0. The master MAC and the root master MAC of event 41 are those of
`awdl0`.

### 3.2 A peer is the master

`programChipWithNewMaster` pushes ioctl 229 on every master change, with the last TLV 4 of that
master ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §3.12):

```
limit of the next slot = rx_time(AF of the master) + dc · 1024 µs
AW starting at that limit = aw (sn of TLV 4)
```

`rx_time` is the reception time on the layer's clock, not the `mactime` of the 229. The `mactime`
comes from the clock of whoever filled the 72 B header, which in this design is the layer itself.
The layer can use directly its own reception mark of the last AF of that MAC. `tx_delay` (difference
between the real TX and the master's planned one) is subtracted to avoid dragging the sender's delay
`?` (sign and exact use to confirm with a real peer).

While the master does not change, the 229 does not repeat. The layer must re-adjust the clock with
every PSF/MIF of the master it receives, reading `+4` (dc) and `+32` (sn) of its TLV 4 before
injecting it. That is what owly does (`Clock::update`) and what Broadcom's firmware does. ESP32Drop
adds a phase filter (PLL) with the PSFs, because the ESP32 clock drifts (`awdl_gauge.h`).

### 3.3 The manager also fixes the master with 134 and 155

After the 229 come the 134 (`master`, `op_mode`) and the 155 (election parameters). The MAC of the
134 is the one that must appear in event 41 `+0x08`. If another one appears, `monitorTimeline` logs
«Waiting for Master» or «Master change with no reason» and corrects nothing.

## 4. What to do in each slot

The project's own sequence is fixed by ioctl 129: 16 entries of 12 B with the channel at `+4`, and channel 0
in the unserved slots. At the start of each slot:

| Channel of the slot in the 129 | Action |
|---|---|
| ≠ 0 and it is the radio's channel | Emit event 41, open the window and allow TX |
| ≠ 0 and another channel | Change channel if the layer supports it; if not, treat the slot as unserved. The announced sequence must carry 0 there ([AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) §7) |
| 0 | Nothing: no event or TX |

`monitorTimeline` predicts the next `AwNumber` by jumping to the next slot with a non-zero
channel of the current or previous sequence. Therefore the layer **must emit the 41 only in served
slots**, with `AwNumber` = first AW of the slot (multiple of 4). If it emitted it in all of them,
the manager would find no discontinuities, but would believe it is in window in slots where the
radio is on another channel.

## 5. Events

### 5.1 Event 41 at the start of a served slot

36 B payload ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2):

| Offset | Value |
|---:|---|
| `+0x00` | `AwNumber` of the first AW of the slot (`u16`) |
| `+0x04` | sync state (`u32`): 1 if the layer is master (§3.1), 2 if it follows another master (§3.2); 0 = stopped, 3 = NeMaster ([AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2) |
| `+0x08` | MAC of the master (that of the 134, or that of `awdl0` if it is master) |
| `+0x0e` | MAC of the root master (`root` of the 229, or that of `awdl0`) |
| `+0x14` | `FwAwStartTime` = slot start in µs, **big-endian** |
| `+0x18` | version of `apple80211_channel` (that of the sequence entry) |
| `+0x1c` | channel number |
| `+0x20` | channel flags (those of the sequence entry) |

Effects on the manager: `inAvailabilityWindow()` becomes true and it starts requesting packets
(`dequeueTxPacket`). If slot 0 starts less than 64 ms after the previous one, the manager ignores
the event («slot 0 start too soon»). With slots of 65.5 ms it does not happen, unless the timer
runs early.

### 5.2 Event 42

Optional. Its only write is the `ivars+408` counter that `dequeueTxPacket` compares with 4. Without
firmware extending windows, the layer can omit it `?`.

### 5.3 Event 43 at the window close

40 B payload; all zeros skips the CCA branch. Effects: it closes the window (`ivars+772 = 0`)
and, when it is due, runs `runElection`. It is the only periodic election trigger besides the
parsing of the received TLVs 4. Without the 43 the election is not re-evaluated.

The layer must close the window no later than the end of the slot (64 TU). The common AW lasts
16 TU, and the extensions of the TLV 4 (`03 03 03 03`, 3 AWs) cover the rest of the slot. Keeping
the window open for the whole served slot takes advantage of the peer being on the channel during
the 4 AWs.

## 6. TX guards inside the window

owly (AWDL-16): unicast only inside a shared AW, more than 3 TU away from the edges. Multicast with
16 TU of guard in slots 0 and 10, or in any served slot with 3 TU if the radio does not change
channel. The layer applies these guards when draining the TX queue after event 41, because the
manager does not know them: it delivers the packets as soon as `inAvailabilityWindow()` is true.

## 7. Needed precision and time source

- The host clock suffices: owly proves it on Linux with host arrival marks (≤ 3 TU), and
  winawdl on Windows with a high-resolution timer, because a normal Windows wait overshoots by up
  to 16 ms.
- The RTL8814AU has per-port TSF (`REG_TSFTR1` for port 1) and TSFT marks in the radiotap of the
  RX ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §3). Using the hardware mark of the
  RX as `rx_time` removes the USB latency from the phase computation `?` (unmeasured).
- The host timer on XNU (`thread_call` with deadline) has a latency not measured on this machine
  `?`. The margin is 5120 µs for the manager not to log shifts and 3 TU for the frames to land
  inside the peer's AW.

## 8. Infrastructure channel on the test machine

Read with `system_profiler SPAirPortDataType` on 2026-10-30: `en2` associated on channel 153,
80 MHz,
802.11ac. `awdl0` exists and is `UP` (MAC `56:0d:25:xx:xx:xx`).

The 80 MHz 149–161 block contains the social channel 149, but the link primary is 153. The AWDL
frames go at 20 MHz on primary 149, and a radio tuned with primary 153 does not decode them `?`
(standard 802.11 behavior; not measured on the RTL8814AU).

With the radio fixed on the infrastructure channel (owly's «shared» mode,
[AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §6), the shortest path is configuring
the router with **primary 149** (or 44) at 20/40/80 MHz. That way `en2` and `awdl0` share a
channel: the iPhone visits 149 in several slots of every 16 and the radio does not have to hop.
With the router on another primary, channel hops or exclusive mode are needed (the station off the
channel during the AWDL session). The ESP32-S3 only uses channel 6, which with infrastructure on
5 GHz requires leaving the channel in each served slot.

## 9. Implementation order

1. Free clock (§3.1) + events 41/43 in the served slots: the manager stops being out of
   window and requests packets (PSF/MIF TX per the draft `AWDL_PSF_MIF_EMISSION.md`, in progress).
2. With an ESP32-S3 nearby: if it follows the Mac as master, the free clock and the TX are coherent
   ([AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §3).
3. Master following (§3.2) when a peer with a better metric appears (an iPhone or a Mac).
