# AWDL: state reached and lockup due to the lack of an Apple peer

Situation snapshot at 2026-10-30. The AWDL work on `RTL8814AirLayer.kext` got as far as one can
get without a second device that speaks AWDL. What follows needs a real peer and the project does
not have one. This document records what is done, what is stalled, why there is no substitute and
which tests run as soon as an Apple device becomes available.

Development machine: macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64), `RTL8814Native.kext` 4.34,
`RTL8814AirLayer.kext` 0.32.0, TP-Link T9UH (RTL8814AU), `rtlair=2 rtlawdl=1`. Anything not seen
carries `?`.

## 1. State reached

Validated on hardware (detail in [AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md)):

| Milestone | Result |
|---|---|
| `awdl0` created and published by the layer (stage 1) | Interface present; `wifip2pd` finds it and does not recreate it |
| `IO80211AWDLPeerManager::configure` (stage 3) | Finishes: `configure done!`, `_enabled = TRUE` |
| `interfaceStateChange` with ioctl 116 | Passes; `_stateFlags = 0x20` no longer remains |
| `enableAWDL` (155, 129, 117) | `AWDL ON:[infra(149) ...]`; request 120 returns 0 |
| Service searches in `wifip2pd` | Registers `_airplay-p2p._tcp`, `_raop._tcp` and `_airplay._tcp` |
| Solo election | `runElection` with `peers:0` and own MAC as master, once per second |
| Stability | No panics or hangs during boot and the tests |

The eighteen setters of the AWDL surface answer success without touching the radio (macro `AWDL_ACK` in
`Source/AirLayer/AWDLInterface.{h,cpp}`). Commits: `42553ba` (setter acknowledgment) and `39185f9`
(results note). Deployed kext: md5 `647f1ab25c3aa2c32a252b2e60371d8b`.

## 2. What remains stalled

Everything that follows requires an AWDL peer on the air to be verifiable; without it there is no
way to know whether an implementation is correct.

| Pending | Why it needs a peer | Document |
|---|---|---|
| Events 41/42/43 (availability window start, extension start and end) | Emitting them without a radio backing the windows makes `inAvailabilityWindow()` return true and the manager start pulling TX; the real cadence of `AwNumber` and `FwAwStartTime` is `?` | [AWDL_STAGE3_CONFIGURE_RESULTS.md](AWDL_STAGE3_CONFIGURE_RESULTS.md) §4, [AWDL_PEERMANAGER_DRIVER_ABI.md](AWDL_PEERMANAGER_DRIVER_ABI.md) §2.2 |
| Real TX of action frames and data over USB (stages 2, 2a, 2b) | Without a receiver there is no way to check that the frame goes out with the right format and channel; `Tools/awdl_stage2_output_hook.patch` was not applied | [AWDL_ETAPAS_2_A_4.md](AWDL_ETAPAS_2_A_4.md), [AWDL_STAGE2B_MBUF_TO_PACKET.md](AWDL_STAGE2B_MBUF_TO_PACKET.md) |
| RX of peer action frames and delivery to `actionFrameInput` | There are no AWDL frames to receive; the chip reception filter is confirmed with real traffic | [AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §6 |
| Election, PSF and MIF against a peer | With `peers:0` the manager never leaves the solo election | [AWDL_PEERMANAGER_SOFTWARE.md](AWDL_PEERMANAGER_SOFTWARE.md) |
| Window timing with the RTL8814AU | Host timer precision against the AWDL cadence: only measurable with a synchronized peer | [AWDL_FIRMWARE_DUTIES.md](AWDL_FIRMWARE_DUTIES.md) §7 |
| Which getters `wifip2pd` consults when opening AirDrop | Item 12 of the [index](AWDL_INDICE.md) §8.2; a real peer closes it | [AWDL_STAGE1_STUB_VALUES.md](AWDL_STAGE1_STUB_VALUES.md) |

Not locked by a peer but open (non-fatal failures the manager logs and continues): read 341
(`awdl_cap`), `getAWDL_SYNC_ENABLED`, `setAWDL_OOB_AUTO_REQUEST`, 414 (`P2P_COEX_PROTECTION`),
election metric 65, HT/VHT capabilities and the types 293 and 575 of `wifip2pd`. These are
low-value cleanups while there are no windows.

## 3. Why there is no substitute

Update 2026-10-30: a partial substitute exists. ESP32Drop implements AWDL on an ESP32-S3 and
works against Mac and iPhone. With this driver it validates TX first: the ESP32 only emits after
electing a master. Detail and test order in
[AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §3 and §10. What follows describes
the situation without that hardware.

- **There are no Apple devices in the project.** AWDL is an Apple proprietary protocol; without an
  iPhone, iPad or Mac there is no counterpart.
- **The available Android phone (A53, SCSC S620) cannot serve as a peer.** There is no known AWDL
  support on Android, and the chip probably does not offer monitor mode for capturing; Wi-Fi
  Aware/NAN is a different protocol and only remained as a hint of other work.
- **AirDrop adds one more dependency.** AirDrop discovery goes through Bluetooth and this machine
  has no working Bluetooth (the Finder shows «Bluetooth: disabled» and visibility at «nobody»).
  Without Bluetooth, request 120 still goes out (seen in the tests), but the AirDrop window lists
  no peers. Other services using AWDL (peer-to-peer AirPlay) may not depend on it `?`.
- **Nothing is validated in a vacuum.** An incoherent event emitter leaves the peer «unsynced»
  (deviation ≥ 6554 µs modulo 2^20 µs); without a real peer, neither the cadence nor the deviation
  is measurable.

## 4. What is needed and what is done once available

Device: any iPhone, iPad or Mac with AWDL active (AirDrop, peer-to-peer AirPlay, Sidecar). A Mac
is the most comfortable peer: it allows observing its own `awdl0` with `tcpdump -i awdl0`
(planned, untested `?`). For full AirDrop, a working Bluetooth adapter on the dev machine.

Planned test order with the peer present:

1. **RX first.** Put the chip on the social channel (6 or 149) and check that peer action frames
   arrive at `RxCompletionQueue` (E2.4 of [AWDL_ETAPAS_2_A_4.md](AWDL_ETAPAS_2_A_4.md)). It is the
   lowest-risk test and confirms the live value of `RXFLTMAP0` (item 20 of the index).
2. **TX of action frames and NDP.** Apply the stage 2 output hook and verify on the peer that the
   frames go out (E2.3). Closes the TX confirmation by C2H or `CONFIG_XMIT_ACK`.
3. **Events 41/42/43.** Emit them with the window timer once RX and TX work, and measure
   `AwNumber`/`FwAwStartTime` against the peer.
4. **Election and PSF/MIF.** Check that the manager leaves `peers:0`, that `PEER_PRESENCE` (47)
   appears and that `wifip2pd` registers peers.
5. **AirDrop and AirPlay.** With Bluetooth available, test discovery and a transfer.

The patches and the deployment procedure remain valid: `Tools/awdl_stage3_configure.patch`,
`Tools/awdl_stage3b_sync.patch` and `Tools/awdl_stage3c_acks.patch`, deployed with
`RTL8814_AWDL=1 Tools/phase5_deploy.sh <EFI> layer 2`.

## 5. WiFi work that does not depend on AWDL

While there is no peer, the rest of the driver remains useful, verifiable work with the current
machine and router (see `Docs/ROADMAP.md`): WPA3-SAE (steps 4-5: 4-way with KDF-SHA256 and
AES-CMAC, BIP/PMF protection; probably feasible with an SAE-only hotspot), uplink retransmissions
(≈36 % retries on the AP; instrument ACK/BlockAck), roaming on 2.4 GHz and macOS 14/15 support.
Sleep/wake was already tested and does not figure as pending.
