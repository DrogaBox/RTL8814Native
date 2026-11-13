# The manager's data mode: why the sequence did not widen

Measured and read on macOS 26.7.1 (25G241), T9UH and the virtual peer, on 2026-11-06. Addresses are from the 25G241 IDB.

## 1. What was measured

During the two transfers in [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §8.1, 1 MiB to the Mac and 4 MiB from the Mac, the kernel log did not change state.
With en2 on 149, `AWDL ON` says:

| State | Sequence | Channel in how many slots of 16 |
|---|---|---|
| `Low Power`, for the whole transfer | `149 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0` | 3 on 149 |
| `Idle` | `149 149 149 0 0 0 0 0 6 149 149 0 0 0 0 0` | 5 on 149 |
| `Discovery` | `149 149 149 149 0 0 0 0 6 149 149 149 0 0 0 0` | 7 on 149 |
| `Data + Infra Idle`, only for moments at boot | `149 149 149 149 149 149 149 149 6 149 149 149 149 149 149 149` | 15 on 149 |

With 3 slots of 16, upload stays at 113 KB/s. The iPhone widens with traffic (§15 of [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md)), and the Mac did not.

## 2. What decides data mode

`IO80211AWDLPeerManager::monitorAWDLDataMode` (`0xffffff80021e35aa`) runs from `reportDataPathEvents`, on the window start event, every as many slots as
`ivars+16064` says. It compares two counters of the base, at `*(pm+0x18)`, with the previous pass:

| Field | Content | What increments it |
|---|---|---|
| `+1968` | Unicast TX packets | `skywalkLogTxPacket`, from `logSkywalkTxReqPacket` |
| `+1948` | Unicast RX packets | `skywalkInputPacket` |

If there was TX and TX plus RX reaches `ivars+16060`, it calls `setDataMode(6, 0)`, whose reason is `RptDataPathThres`. `setDataMode` goes to state 5, `Data`,
or to another data state if a scan is running. `initParamsToDefaults` leaves `ivars+16060` at 8, `ivars+16064` at 8 and `ivars+16068` at 4. With no
traffic for `16 × presence × (ivars+16064 + ivars+16072)` ms, it goes back to `Idle` and later to `Low Power`.

## 3. The cause

The layer goes through neither `logSkywalkTxReqPacket` nor `skywalkInputPacket`: `txData` only calls `preQueuePacketCheckQuietMode`
([AWDL_TX_HOOKS_POR_PAQUETE.md](AWDL_TX_HOOKS_POR_PAQUETE.md) §2) and `deliverData` hands straight to the RX queue. The counters stay at 0, so for the
manager there is never any traffic.

`preQueuePacketCheckQuietMode` calls `setDataMode(3, 0)` only if the manager is in quiet mode, so that path is not enough either.

## 4. The change

`txData` and `deliverData` increment the packet and byte counters (`+1968` and `+1988`, `+1948` and `+1992`) for each unicast packet. These are fixed 25G241
offsets, and other system versions will need their own.

The other readers of those fields have no effect on the data path: `gatherInfraStats`, `updateServiceRegistrationStats`, `updatePacketStatsPerMonitor`, and
the logs in `interfaceMonitor`, `printPackets` and `printDataPath`. None compares them with TX completions, so no stall detector fires from counting them
without going through the rest of the path.

`logSkywalkTxReqPacket` itself stays uncalled, since it requires the packet's `PacketSkywalkScratch`, which the layer does not handle. `peer_ivars+184`, the
per-AC count and that function's BPF tap are not updated.

On 24H232 (15.8.2) the same fields sit 16 bytes earlier, read in IDA and not run: `+1952` and `+1972` for TX, `+1932` and `+1976` for RX, with the `current Tx count` and `current Rx count` strings in `monitorAWDLDataMode` (`0xffffff80021801ce`).

## 5. Measured with the change

Layer `43ACC5D0`, en2 on 149, 2026-11-06.

| Test | Before | With the change |
|---|---|---|
| 4 MiB of TCP from the Mac to the virtual peer | 113 KB/s | 507 KB/s on average, 8.1 s, identical file, MSS 1356 |
| State during that transfer | `Low Power` throughout | `Data + Infra Idle` after about 1.3 s, with `RptDataPathThres` |
| 4 MiB by AirDrop to the Mac | not measured at that size | `/Upload -> 200` in about 2 s, identical file |
| Unicast to the router, 50 frames/s | 494 frames with an ACK next, one copy each, no retries | 495 frames of 494 sequence numbers, 494 ACKs, 1 retry |

During the router test the Mac stayed in `Data + Infra Idle` from start to end. Once finished it goes to `Idle` in about 2 s, and to `Low Power` about 5 s
later through `IdleTimeout`. The `RptDataPathThres` steps come every 1.05 s, that is, every 16 slots. How that fits with `ivars+16064` is still to review `?`.

The path to the Mac does not go over the air with the virtual peer, so that row only checks that the RX count does no harm.

The TX ring had 6 full-ring drops after the TCP from the Mac and 567 after the AirDrop and the router test, which runs at 50 frames/s. By elimination, those 561
are TCP ACKs from the AirDrop, whose upload happens mostly in `Idle`, with gaps of several slots without ticks. The ring drops the oldest frame and a TCP ACK
is cumulative, so the file arrived anyway. Not counted separately `?`.

## 6. The limit that remains

Speed is now set by the budget in `sendData`: 12 ms of air per tick at 12 Mb/s. A 1468-byte frame costs 1138 µs, so 10 fit in a tick. Each served slot has
3 ticks, because the last AW sends no data.

| State | Ticks in 16 slots (1.05 s) | TCP ceiling with MSS 1356 | Measured |
|---|---|---|---|
| `Low Power`, 3 slots | 9 | 114 KB/s | 113 KB/s |
| `Data`, 15 slots | 45 | 568 KB/s | about 580 KB/s, worked out without the first 1.3 s |

The 12 Mb/s rate and the budget do not change with this fix. Going beyond that needs another data rate in the radio.

## 7. Where data sits in data mode

Frames leave only on ticks. Folded over the 64 TU period of the router capture, they form three groups, 13.5, 16.5 and 34 TU apart. That matches the end of
the guard at TU 2.5 and the ticks at TU 16 and 32, followed by the AW without data. The guard in §16 of [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) still
acts, and only in the first AW of each slot.

`slotfine.py` reports 244 of 494 before TU 3 here, because it works the position out from the PSF and a delayed PSF moves it earlier. Frames from TU 16 and from
the end of the guard appear in that count as TU 13-15 and 1-2, with the same offset of about 1.5 TU. To compare positions in this state the fold is what counts, not the absolute
value.

## 8. Not measured

- With a real iPhone: whether the iPhone and the Mac share more slots, and the speed over the air.
- With the network on a channel that is not social: a wider sequence increases the time away from that channel `?`.

## 9. Data to a peer that is not in the slot

Cost on the BSS with en2 and `awdl0` on 149, measured with a ping every 0.2 s to the router over en2 and the A53 in monitor:

| Traffic on `awdl0` | Data frames on the air | Copies per frame | Ping over en2 |
|---|---|---|---|
| None | 0 | - | 0 % loss, 1.9 ms on average |
| UDP to the router, which answers with an ACK | 430/s | 1 | 0 % loss, 8.7 ms on average, 336 ms at most |
| TCP to the virtual peer, no ACK | 808/s | 2 | No en2 frame goes out during the transfer; RTT of up to 6.7 s |

The 430 frames/s match the ceiling in §6, and with ACKs the network keeps working. The virtual peer is another case: the limit of 1 retry in `AWDLTx.c` gives 2 copies, the
budget counts one and the management queue stays full with no pause. So it cannot measure the cost on the BSS.

A frame with no ACK to a real peer spends the 7 copies of the normal limit, more than in that row. At rest the iPhone shares with the Mac only slots 0, 2 and 10
(§15 of [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md)). Until this fix, the Mac's 3 slots were exactly those. With the Mac in `Data` and the iPhone still at rest, or with the
iPhone on another network, 12 of the 15 slots would send to nobody, and crosswind already avoids that case (AWDL-16).

The layer now keeps the TLV 18 each peer announces (`notePeerSeq`). `sendData` leaves the frame at the head of the ring if its destination announced another channel for that
slot, and a group destination asks about all peers. When in doubt, it sends: with nothing recent from the peer in 3 s, if its AW number does not land on the Mac's slot, with no
slots of its on the channel or none common to them all. The periodic log line adds `held N`.

Measured with layer `07BC6107`, 4 MiB of TCP to the virtual peer. `awdlsim --rest` makes the node announce only slots 0, 2 and 10; the Mac announces the 15 of `Data` in both rows:

| Node | TCP | First copies per slot | Ping over en2 |
|---|---|---|---|
| With `--rest` | 113 KB/s and 112 KB/s, identical | 0: 357, 2: 341, 10: 336, 11: 4; none in the others | 0 % loss, 6.7 ms on average, 71 ms at most |
| Without `--rest` | 542 KB/s, identical | Between 137 and 225 in slots 0-7 and 9-15, 78 in slot 8 | No loss, but up to 7 s of delay, from the 2 copies above |

With `--rest`, the cost on the BSS stops being that of the third row of the first table even though the node still gives no ACK. The 78 frames in slot 8 and the 4 in slot 11 fall outside
what the Mac or the node announce, and are put down, without having been checked, to the full queue draining late or to the offset in §7. With UDP to the router, on this layer: 430
frames/s, 0 % loss, 3.7 ms on average and 16 ms at most.

The log line cuts at 192 characters, which left `held` out. Layer `2E3D2058` moves it next to the data counters, and is loaded: it reads `held 0` without a peer. The number of holds is only measured
in the split by slots above, and with a real peer it is unmeasured.

The budget still does not count retries, so a present peer that loses many frames can still fill the air `?`.
