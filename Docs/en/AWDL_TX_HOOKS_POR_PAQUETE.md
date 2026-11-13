# Per-packet hooks on the TX of awdl0

What `IO80211VirtualInterface::logSkywalkTxReqPacket` updates per packet, what
`IO80211AWDLPeerManager::preQueuePacketCheckQuietMode` does, whether the manager removes or marks inactive a peer
with no TX toward it and what minimal calls a driver transmitting data on its own needs. Base:
copy of the kernel collection 25G241. Unobserved items go with `?`.

## 1. `logSkywalkTxReqPacket` (`0xffffff800233218a`, slot +0xB60)

The function takes the packet, the `PacketSkywalkScratch`, the Ethernet header, the WME class and a boolean
(`a6`). Observed order in the decompilation:

1. `result = *(this+0x120)` and `peerManager = *(result+0x28)`.
2. Peer lookup by the header: `peer = (*(vtable+0x120))(peerManager, ether_header)` in the
   indirect call of `0xffffff800233218a` (slot `+0x120` of the peer manager receives the header,
   not only the DA). If there is a peer, it writes `mach_absolute_time()` into `*(peer_ivars+184)`
   (`*(*(peer+0x18)+184)`), stamp of last TX toward that peer.
3. BPF: if `(*(*(this+0x128)+60) & 2) != 0`, it calls `bpfTapOutputPacket(this, 0x93, packet, 0, 0)`.
4. Statistics: `IO80211PeerManager::skywalkLogTxPacket(peerManager, packet, scratch, wme, peer, a6)`.
5. If `a6 != 0`, it writes `mach_absolute_time()` into `peerManager_ivars+2280` and `+2288` (two
   consecutive stamps).
6. If the role of the interface (`*(*(this+0x120)+88)`) is 6, it calls
   `preQueuePacketCheckQuietMode(peerManager, ether_header)`.

Readers of the fields: `+184` has not been traced to a concrete reader `?`; the
stamps `+2280/+2288` do not appear in peer aging `?`; the statistics of `skywalkLogTxPacket` feed
the reports of the manager. The peer aging that has been verified uses `peer_ivars+784` (last AF),
not `+184` (§3). The `0x93` of BPF is the output tap type `?` name.

## 2. `preQueuePacketCheckQuietMode` (`0xffffff800221649e`)

With `ivars = *(manager+0x20)`:

- If `*(ivars+42568) != 0` (AirPlay service counter) it does nothing.
- If `*(ivars+42568) == 0`, it reads `*(ivars+16096)`. It exits unchanged when
  `(v & 0xC) == 0` or `(v & 8) != 0`; otherwise (`(v & 0xC) != 0` and `(v & 8) == 0`)
  it considers quiet mode must be exited.
- If the destination MAC is unicast (`(ether_dhost[0] & 1) == 0`), it calls `setDataMode(this, 3, 0)`.
- It logs "Exiting quiet mode due to queued packet from network stack" and calls
  `exitQuietMode` (`0xffffff80021fad1c`).

Skipping it leaves quiet mode active even though the network stack has a packet enqueued; the packet
waits for another route to take it out (timer, event or the next packet if the driver does call it). No
permanent block has been measured `?`; the risk is a delay or a stalled queue until an exit through another
route.

## 3. Is a peer with no TX removed or marked inactive?

No path has been observed that removes or marks inactive a peer **only** for lack of TX toward it:

- `IO80211AWDLPeer::shouldRemovePeerFromSystem` (`0xffffff80021aa5e2`) measures the time since
  `peer_ivars+784` (last AF) and returns true from `0x186A1` ms = 100,001 ms (~100 s);
  it does not read `+184`.
- `IO80211AWDLPeer::shouldRemovePeerFromNDP` (`0xffffff80021a9c50`) uses `+784` and `+56`; the concrete
  thresholds have not been extracted `?`.
- `ageoutRootNodeList` (`0xffffff80021ebd0e`) expires entries of the root list at `0x36EE81` ms =
  3,600,001 ms (~1 h).
- `ageoutPacketsWaitingForPPP` (`0xffffff80021aa7ca`) drops waiting packets at `0x7D1` ms =
  2,001 ms.
- `shouldRemovePeerFromSystem` uses `reachability` (`peer_ivars+120`) only in the log; the decision is the
  last-AF stamp.

Therefore, the lack of TX does not by itself cause the peer removal with the verified paths; the
peer is removed by AF age. If `logSkywalkTxReqPacket` is not called, `+184` becomes stale and
any reader of that stamp (unidentified `?`) would see old activity, but the AF aging keeps
working.

## 4. Minimal calls per packet

The call concentrating the work is the `logSkywalkTxReqPacket` of the virtual interface itself, slot
`+0xB60` (VA `0xffffff800233218a`), with:

| Argument | Value |
|---|---|
| `this` | the `IO80211VirtualInterface` of `awdl0` (in this layer, `RTLAwdInterface`) |
| packet | the `IOSkywalkPacket`/`IO80211NetworkPacket` being transmitted |
| scratch | the `PacketSkywalkScratch` of the packet |
| header | `ether_header *` with the real DA of the frame |
| WME | the `apple80211_wme_ac` class used |
| boolean | `a6`: activates the stamps `+2280/+2288` and the special treatment |

That call does the peer lookup, the `+184` stamp, the BPF tap, the statistics and, in role 6, the
quiet-mode check. If the driver does not call it, traffic can keep going out, but the
manager does not see the TX activity of that packet: it does not update `+184`, does not count the statistics and does not exit
quiet mode via that route. The minimal alternative is calling `skywalkLogTxPacket` and, in role 6,
`preQueuePacketCheckQuietMode`, but the `+184` stamp and the BPF tap are lost.

No lock taking was observed inside `logSkywalkTxReqPacket`; `skywalkLogTxPacket` and
`preQueuePacketCheckQuietMode` run on the TX path and the manager uses its own gates `?`
(neither the exact gate nor whether the caller must hold it was isolated). The function is a
vtable method, not a KPI: it is reached via slot `+0xB60`; whether the symbol is exported to link it
directly from a kext was not checked `?`.

## 5. Pending

- Reader of `peer_ivars+184` and of the stamps `+2280/+2288` `?`.
- Thresholds of `shouldRemovePeerFromNDP` `?`.
- Gate/lock required by `skywalkLogTxPacket` and `preQueuePacketCheckQuietMode` `?`.
- Export of the symbol for direct linking `?`.
