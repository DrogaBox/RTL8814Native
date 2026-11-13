# AWDL: result of the first test with an iPhone

Test of 2026-11-08 with a visiting iPhone (iOS 27, no developer mode, no cable) against the Mac with the
T9UH, macOS 26.7.1 (25G241). The script is [AWDL_PRUEBA_CON_IPHONE.md](AWDL_PRUEBA_CON_IPHONE.md). Addresses
come from the IDB of the 25G241 kernelcache (`boot_awdl.kc.i64`).

The test's captures and logs carry the iPhone owner's data and stay outside the repository.

## 1. Result

| Direction | State | Layer |
|---|---|---|
| iPhone → Mac, photo | Works: BLE, mDNS, QUIC/HTTP3 and TCP, `HELLO`, `ASK` with an accept prompt, `UPLOAD` | 001EF268 |
| iPhone → Mac, 64.3 MB video | Works: 80 s, about 800 KB/s sustained | C84B286E (94e83e7) |
| iPhone → Mac without a manual `ndp -s` | Works with `Tools/iphone_test.sh run` (e287ac3) | 44952481 (7333d24) |
| Mac → iPhone | The iPhone does not show up in the Mac's AirDrop sheet; cause and fix in §3, checked with a virtual peer | 44952481 |

## 2. Defects found and fixed

### 2.1 Data frames with a length wrapper (94e83e7)

iOS sends its large data frames (QoS data at MCS 0/1, A-MSDU bit clear) with an RFC 1042 header in front of
the Apple SNAP: `AA AA 03 00 00 00 LL LL`, where `LL LL` is the length of the AWDL MSDU that follows. The
layer dropped them as `bad`. An A53 capture during an upload counted 620 of 1370 iPhone frames to the Mac
in that format, and none reached `awdl0`. With the fix the layer's RX line shows `wrapped`, and the video
went through at about 800 KB/s with no drops.

### 2.2 Presence mode rejected (7333d24)

`IO80211AWDLPeerManager::setPresenceMode` (`0xffffff80021cc5a6`) sends ioctl 200 to the driver and stores
the mode in byte `+17032` of its ivars only when the driver accepts it. `configure()` zeroes that byte first,
at `0xffffff80021c6175`. The layer answered `kIOReturnUnsupported`, so the mode stayed 0. The manager asks
for 4.

`actionFrameProcessInput` (`0xffffff800220e8b0`) compares the AW sequence of each frame from a cached peer
with the AW of the last event 41 (`+16104`), which arrives once per slot:

```
window - 2 <= seq <= window + presenceMode    → SyncErrors (peer +280) = 0
outside that range                            → SyncErrors++ (MI only) and a SYNC_INFO line
```

With mode 0, every iPhone frame from the second AW of a slot on counted as an error. The family log showed
`SYNC_INFO ... SyncErrors(N)` climbing about eight times every 4 s. With layer 44952481 the manager sends 4,
the layer accepts it, and no `SYNC_INFO` line appears for the iPhone.

`rtg` and `isRTG` in those lines refer to real-time (RTG) peers, not to whether the peer is routable.

### 2.3 Missing neighbour entry

The iPhone's SYN reached the Mac (`SYN_RCVD`), but no SYN-ACK left until a manual
`ndp -s fe80::<EUI-64>%awdl0 <MAC>`. `awdl0` sends no neighbour solicitations. Nothing in the kernelcache
calls `ifnet_notice_node_presence` (`0xffffff8000643ca0`, 0 references); the AWDL neighbour table belongs to
`wifip2pd` (strings `NeighborDiscoveryTable`, `Posted peer presence for %s on %s`). Until the presence
notice (§3) is solved, `Tools/iphone_test.sh run` adds a permanent entry for every source the radio hears.

## 3. Why the Mac does not discover the iPhone: the neighbour entry is never published
The iPhone does not advertise its AirDrop receiver over multicast mDNS: in 15 minutes of capture on
channel 149 it sent a single multicast frame. It advertises it inside its AWDL action frames, in TLV 2
(service response): `<id>._airdrop._tcp` with a compressed name (`c0 07`) and TXT `flags=111611`, in more
than 1700 frames. `wifip2pd` starts `AWDLBrowse _airdrop._tcp.local` when the AirDrop sheet opens, but it
only resolves a service of a peer it has discovered, and that same publication is what creates the `awdl0`
neighbour entry (§2.3). Both failures share one origin.
1. The family hands an action frame to the daemon as event 48 only when `actionFrameReport` set no error
   (`ctx+0x2C == 0`) and the peer ended with `peerReachability == 1`
   ([AWDL_AF_TO_DAEMON.md](AWDL_AF_TO_DAEMON.md) §2). The iPhone reached that state: the
   `shouldRemovePeerFromNDP ... No Action frames for(...)` lines in the logs require `+120 == 1`
   (`0xffffff80021a9c50`), so its frames did go out as event 48.
2. In `wifip2pd`, the event 48 handler (`sub_10041C470`) publishes the peer's presence (neighbour table,
   `ndp` over the routing socket, ioctl 415) only when the interface byte at slot `[class+2F8h]` is not 0
   and the one at `[class+2C8h]` is 0 ([AWDL_WIFIP2PD_PRESENCE_ONCE.md](AWDL_WIFIP2PD_PRESENCE_ONCE.md)
   §3.1). The second one is "Driver interface was removed" and did not show up in the log. The first one
   is written once, in the interface initialiser (`sub_100416D00`, `0x100417647`), with witness 4 of the
   `AWDLWiFiDriver` conformance of the driver type (protocol `AWDLController`, associated type `Driver`):
   | Driver type | Witness 4 | Value |
   |---|---|---|
   | `AppleIO80211Driver` | `sub_1000A93F0` | `RawGet(117)` without error and `u32 @+8` (`enabled`) not 0 |
   | `AppleBroadcomDriver` | `sub_100003B30` | constant 0 |
3. GET 117 (`APPLE80211_IOC_AWDL_SYNC_ENABLED`, 12 B) is answered by the driver: `getAWDL_SYNC_ENABLED`
   (`0xffffff800227bc2f`) → `IO80211AWDLPeerManager::getAwdlSyncEnabled` (`0xffffff800221f0f4`) →
   `apple80211RouteInternal(0xC02869C9, 117)` → `apple80211getAWDL_SYNC_ENABLED` (`0xffffff8002263899`) →
   slot `+0xFF0` of the interface. The manager only compares the answer with its own state and logs
   `enable state mismatch` when they differ. The layer answered `kIOReturnUnsupported`.
With that answer the daemon's flag stayed 0 for the life of the interface: no peer ever got its neighbour
entry without `ndp -s`, the 415 was never sent, and `pppDone[8829] Posted Peer Presence` appears in no log
(only `Posted Peer Absence`). This matches every earlier test too, with the A53 and with `awdlsim`, which
always needed the entry by hand.
- `getAWDL_SYNC_ENABLED` answers `version 1, flags 0, enabled 1`. The daemon's GET can arrive before the
  manager's SET 117, so the answer does not depend on state: sync runs in the layer.
- The 72-byte header of each action frame carried channel flags 0. `IO80211Peer::reportRssi`
  (`0xffffff8002242c4e`, called from `actionFrameReport` at `0xffffff80021afbb0`) updates the 5 GHz RSSI
  average (`+1188`) only with bit `0x10` and the 2.4 GHz one (`+1180`) only with `0x08`; without them the
  average stayed at the value `addAWDLPeer` copies from the first frame. The daemon derives the band from
  the same flags. The layer now sets the band, `0x02` (20 MHz) and `0x100` (DFS, channels 52 to 144), like
  `ChanSpecConvToApple80211Channel` (`0xffffff8002198efa`).
Checked on 2026-11-09 with layer `233601f6` loaded and `awdlsim` as the peer (`--metric 1 --6e`, no
`ndp -s`): a few seconds after the node's first frames the layer logged `getAWDL_SYNC_ENABLED -> 1` (four
reads by the daemon after boot), `wifip2pd` logged `Posted peer presence for <node MAC> on awdl0`, `ndp -an`
showed the node's `fe80::` entry as `permanent R`, and the family logged `pppDone[8829] Posted Peer
Presence for <MAC>, uploading 0 packets to stack`. The entry that `Tools/iphone_test.sh run` adds on its own
(e287ac3) is no longer needed. Whether the iPhone shows up in the Mac's AirDrop sheet is still to be seen `?`.
Checked later with a virtual receiver ([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10): the Mac shows in its sheet a
peer that announces `_airdrop._tcp` in its TLV 2s, but `sharingd` drops that announcement when the TXT flags carry bit
`0x10000`, as the iPhone's do (111611), and expects the device through Rapport (`_applicationservicepairing`,
`_appsvcprepair`). The iPhone then enters the sheet by that other path, which is still untested `?`.
- `processServiceResponseTLV` stores the iPhone's TLV 2 when bit 0 of `+16388` is set
  (`mDNSDirectPathEnabled`, on by default: bit 1 of `awdl_mdns_mode`, which is 3 without a boot-arg).
- The family posts event 47 (`APPLE80211_M_PEER_PRESENCE`) only for absence, from
  `cleanUpForPeerRemovedFromNDP` and `IO80211PeerManager::removePeer`; its case `'/'` in
  `reportDataPathEvents` only forwards whatever the driver sends. No kext in the kernelcache calls
  `ifnet_notice_node_presence`.
- `shouldStopAFProcessing` (`0xffffff80021b48b8`) skips the RSSI logic for peers with bit 0 of `+49`,
  written only by `processInfraChangesOfPeer` (`0xffffff80021bca21`, bit 6 of the third argument), and
  for types 4 and 8. `PeerType:Cached` in `shouldRemovePeerFromNDP` is the default label when the peer is
  not RTG, TimeSync, Sidecar or AirPlay, not the cache state (`isCached=No` in `SYNC_INFO`).
- `actionFrameReport` fills the presence notice (`ctx+56`, `ctx+92..`) only when the peer has common
  slots (`numOfOverlapSlots`, `+54` of the AWDL ivars, counts the slots with the same channel and band as
  the own sequence at `PM+42868`). The captured sequences match slot by slot: iPhone
  `[6 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0]`, Mac `[0 0 149 0 0 0 0 0 0 0 149 0 0 0 0 0]` when idle.

## 4. That night's pairing by code (read on 2026-11-11)

Since iOS 26.2 and macOS 26.2, AirDrop between two people who are not in each other's contacts asks for a
six-digit code: the receiver shows it, the sender types it, and the two devices stay known to each other for
30 days (Apple's documentation). It is `rapportd`'s PIN pairing
([AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §10.5).

The stored `rapportd` log of that night (`log show`, category `RPPairing`, default-level messages) has the
whole sequence, with the Mac as the receiver:

| Time | `rapportd` lines | Reading |
|---|---|---|
| 20:42:23 | `Server PairVerify session … activated and ready` | The iPhone's first session against the Mac |
| 20:42:34 | `New pairing info generated`, `LISTEN: Updated PIN info`, `PairVerify timeout for session` | The iPhone is not known and the Mac generates the code |
| 20:42:47 | `Looking for PAKE credential` | The code arrives from the iPhone |
| 20:42:48 | `Pairing SUCCESS, returns temporary self identities` | Paired |
| 20:42:49 | `Saving shared temporary identities`, `Saved contact with identifier`, `Added/updated temporary identity` (2) | The Mac stores the iPhone's temporary identities |
| 20:46 to 23:36 | `Server PairVerify succeeded for session` (39), `resolveBonjourCompleted` (8) | Each transfer from the iPhone goes through PairVerify first, now with those identities |

The Rapport path therefore worked with the real iPhone and the layer in the iPhone → Mac direction.

In the other direction the Mac never tried: 10 `Attempting discovery with` lines, all 10 followed by
`Skipping PairVerify for our own bonjour device`, no `Starting PairVerify with:` and
`Updated browse client with 0 endpoints` all 8 times. The iPhone's service records did not reach `rapportd`
(§3).

State on 2026-11-11, on macOS 26.7.3 (25G342) and several reboots later: `rapportd`'s state dump
(`RPKeychain State`) lists two `RPIdentity-AdHocPaired` identities with the masked name `iP*ne`, as many as it
stored that night. `sharingd` asks `rapportd` for devices with the flags
`BLE Friends SessionPaired AdHocPaired`, and that iPhone falls under the last one.

Expected in the next test, Mac to iPhone, unchecked `?`: `Starting PairVerify with:`,
`Client PairVerify succeeded`, `Updated browse client with 1 endpoints` and the iPhone in the sheet, with no
new code while the identities last. With the virtual peer it is checked up to the TLS ClientHello. If the
identities are missing or expired the code remains: the iPhone shows it and it is typed on the Mac `?`.
