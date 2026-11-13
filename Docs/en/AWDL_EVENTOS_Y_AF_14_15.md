# Port of timer events, postMessage path and AWDL action frames to macOS 14.8.9 and 15.8.2

Comparative study and correspondence table between macOS 26.7.1 (build 25G241), macOS 14.8.9
(build 23J631) and macOS 15.8.2 (build 24H232) on x86_64 architecture for the implementation of
AWDL support without dedicated firmware in the RTL8814AU controller (`RTL8814Native.kext` /
`RTL8814AirLayer.kext`).

The base sources of truth taken from the repository correspond to:
- `Docs/AWDL_WINDOW_TIMER.md`: events 41, 42 and 43, slot/window timer and sync state.
- `Docs/AWDL_STAGE3_EVENT_EMITTERS.md`: event emitter matrix and the `postMessage` call chain.
- `Docs/AWDL_AF_TO_DAEMON.md`: action frame (AF) reception path, input filters and event 48.
- `Docs/AWDL_PSF_MIF_EMISSION.md`: construction and delivery of sync (PSF) and master indication
  (MIF/UMI) templates via internal ioctls.

All the cited addresses have been verified as executable instruction heads (`code && head`) in
the corresponding kernel collections.

---

## 1. Executive summary

1. **Identical event structures and ioctls**: The payload formats of events 41 (36 B), 42 (8 B),
   43 (40 B), 46 (16 B firmware / 0 B helper), 47 (68 B), 48 (72 B header + frame) and 195 (400 B)
   are rigorously identical in the three OS versions. Likewise, the structures of the internal
   ioctls 151 (16 B), 149 (56 B), 225 (56 B) and 342 (16 B) keep exactly the same field layout.
2. **Divergence in virtual interface slots**: On macOS 26.7.1 (25G241),
   `IO80211VirtualInterface::postMessage` sits at slot 355 (`+0xB18`) and links to
   `IO80211SkywalkInterface::reportDataPathEvents` at slot 356 (`+0xB20`). On macOS 14.8.9
   (23J631) and macOS 15.8.2 (24H232), `postMessage` is shifted 8 bytes back, at slot 354
   (`+0xB10`), and links to `reportDataPathEvents` at slot 355 (`+0xB18`).
3. **Divergence in `IO80211AWDLPeerManager` slots**:
   - `actionFrameInput`: slot 71 (`+0x238`) on macOS 26 and macOS 14, but slot 70 (`+0x230`) on
     macOS 15.
   - `reportDataPathEvents`: slot 83 (`+0x298`) on macOS 26; slot 81 (`+0x288`) on macOS 14 and
     macOS 15.
   - `dequeueTxPacket`: slot 85 (`+0x2A8`) on macOS 26; slot 84 (`+0x2A0`) on macOS 14; slot 83
     (`+0x298`) on macOS 15.
4. **Critical shift in `peerReachability`**: On macOS 26 and macOS 15, the `peerReachability`
   field of the peer (`IO80211AWDLPeer`) sits at offset `priv+0x78` (`priv = *(peer+0x20)`). On
   macOS 14 it sits at `priv+0x70` (difference of −8 bytes). The `peerReachability == 1` check
   required to publish event 48 is evaluated at `priv+0x70` on macOS 14.
5. **`postMessage` chain on macOS 14**: `IO80211SkywalkInterface::reportDataPathEventsGated` jumps
   directly to `postMessageSync`, skipping the intermediate `postMessageInternal` function that
   exists on macOS 15 and macOS 26. Additionally, the Broadcom emitter
   `AppleBCMWLANProximityInterface` on macOS 14 invokes the virtual slot `+0xB10` of the interface
   directly instead of the intermediary `IO80211Controller::postMessage`.

---

## 2. Window timer events and consumption (`AWDL_WINDOW_TIMER.md`)

| Item | 25G241 | 23J631 | 24H232 | Same (yes/no/?) | Note |
|---|---|---|---|---|---|
| Availability window start event ID (41) | `41` (`0x29`) | `41` (`0x29`) | `41` (`0x29`) | yes | Constant `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START`. |
| Event 41 payload | 36 B | 36 B | 36 B | yes | `u16 AwNumber@0`, `u32 syncState@4`, `MAC master@8`, `MAC root@14`, `u32 BE FwAwStartTime@20`, `u32 chanFlags@24`, `u32 chanNum@28`, `u32 winFlags@32`. |
| Kernel consumer event 41 | `0xffffff80021dd8f4` | `0xffffff80020ccaaa` | `0xffffff800217adb8` | yes | `IO80211AWDLPeerManager::reportDataPathEvents` (case 41 / `')'`). |
| Event 41 consumer vtable slot | Slot 83 (`+0x298`) | Slot 81 (`+0x288`) | Slot 81 (`+0x288`) | no | `reportDataPathEvents` slot in `IO80211AWDLPeerManager`. |
| `inAvailabilityWindow()` flag | `ivars+772` | `ivars+772` | `ivars+772` | yes | `u8` in `*(_BYTE *)(ivars + 772) = 1` at window start. |
| Extension count reset on event 41 | `ivars+408 = 0` | `ivars+408 = 0` | `ivars+408 = 0` | yes | Set to 0 at window start on the 3 versions. |
| Host start mark (`mach_absolute_time`) | `ivars+42696` | `ivars+32600` | `ivars+32680` | no | Variable offset in the private ivars structure. |
| Slot 0 minimum interval threshold (64 ms) | `ivars+66376` | `ivars+51392` | `ivars+52016` | no | Logs «slot 0 start too soon» if the interval is lower. |
| Slot index in sequence (`slot(t)`) | `ivars+16128` | `ivars+16104` | `ivars+16136` | no | Computation `min(15, (AwNumber % (step+1)*n) / (step+1))`. |
| Window extension event ID (42) | `42` (`0x2A`) | `42` (`0x2A`) | `42` (`0x2A`) | yes | Constant `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSION`. |
| Event 42 payload | 8 B | 8 B | 8 B | yes | `u32 sn@0` (extension count), `u32 fwAwStartTime@4`. |
| Kernel consumer event 42 | `0xffffff80021dd8f4` | `0xffffff80020ccaaa` | `0xffffff800217adb8` | yes | `IO80211AWDLPeerManager::reportDataPathEvents` (case 42 / `'*'`). |
| Extension counter write | `ivars+408 = *a3` | `ivars+408 = *a3` | `ivars+408 = *a3` | yes | `*(_DWORD *)(ivars + 408) = *(DWORD *)a3` on the 3 versions. |
| Extension check on TX | `ivars+408 < 4` | `ivars+408 < 4` | `ivars+408 < 4` | yes | Evaluated in `IO80211AWDLPeerManager::dequeueTxPacket`. |
| `dequeueTxPacket` vtable slot | Slot 85 (`+0x2A8`) | Slot 84 (`+0x2A0`) | Slot 83 (`+0x298`) | no | `0xffffff800221675e` (26), `0xffffff8002100f30` (14), `0xffffff80021b0e40` (15). |
| Window end event ID (43) | `43` (`0x2B`) | `43` (`0x2B`) | `43` (`0x2B`) | yes | Constant `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_END`. |
| Event 43 payload | 40 B | 40 B | 40 B | yes | Firmware CCA and channel usage statistics (zero fill valid). |
| Kernel consumer event 43 | `0xffffff80021dd8f4` | `0xffffff80020ccaaa` | `0xffffff800217adb8` | yes | `IO80211AWDLPeerManager::reportDataPathEvents` (case 43 / `'+'`). |
| Event 43 effect: window close | `ivars+772 = 0` | `ivars+772 = 0` | `ivars+772 = 0` | yes | Resets `inAvailabilityWindow() = 0`. |
| Event 43 effect: secondary flag | `ivars+757 = 0` | `ivars+757 = 0` | `ivars+757 = 0` | yes | Zeroes the active window state flag. |
| Event 43 effect: state word | `ivars+0 = 0` | `ivars+0 = 0` | `ivars+0 = 0` | yes | Zeroes the word at ivars offset 0. |
| Window end mark (`mach_absolute_time`) | `ivars+16400` | `ivars+16368` | `ivars+16408` | no | Stores the slot close instant. |
| Periodic election trigger on event 43 | `runElection` | `runElection` | `runElection` | yes | Calls `IO80211AWDLPeerManager::runElection` if `shouldRunElection` is true. |

---

## 3. Event emitters and postMessage path (`AWDL_STAGE3_EVENT_EMITTERS.md`)

| Item | 25G241 | 23J631 | 24H232 | Same (yes/no/?) | Note |
|---|---|---|---|---|---|
| Firmware emitter event 41 | `0xffffff8001718756` (site `0xffffff80017188d1`) | `0xffffff8001681fe6` (site `0xffffff800168217e`) | `0xffffff80016a8cd6` (site `0xffffff80016a8e51`) | no | 26 and 15 call `IO80211Controller::postMessage`; 14 calls virtual `+0xB10` directly. |
| Firmware emitter event 42 | `0xffffff8001718942` (site `0xffffff80017189ac`) | `0xffffff80016821f0` (site `0xffffff8001682247`) | `0xffffff80016a8ec2` (site `0xffffff80016a8f2c`) | no | 26 and 15 call `IO80211Controller::postMessage`; 14 calls virtual `+0xB10` directly. |
| Firmware emitter event 43 | `0xffffff80017184b6` (site `0xffffff8001718649`) | `0xffffff8001681d50` (site `0xffffff8001681ed8`) | `0xffffff80016a8a32` (site `0xffffff80016a8bc9`) | no | 26 and 15 call `IO80211Controller::postMessage`; 14 calls virtual `+0xB10` directly. |
| Firmware emitters event 46 (4 sites) | `0xffffff80017189e4`, `0xffffff8001718ad6`, `0xffffff8001718bd4`, `0xffffff8001719cec` | `0xffffff8001682280`, `0xffffff800168236e`, `0xffffff8001682462`, `0xffffff80016842b4` | `0xffffff80016a8f64`, `0xffffff80016a9056`, `0xffffff80016a9154`, `0xffffff80016aa272` | no | 26 and 15 use controller glue; 14 invokes virtual `+0xB10` with `esi = 0x2E`. |
| Event 46 firmware payload | 16 B | 16 B | 16 B | yes | `u32@0`: 1 master, 2 slave, 3 master without election, 0 dropped. |
| Family helper event 46 (`postSyncStateChanged`) | `0xffffff8002332054` (site `0xffffff8002332073`) | `0xffffff80021da8bc` (site `0xffffff80021da8d2`) | `0xffffff80022a7b2e` (site `0xffffff80022a7b44`) | yes | `IO80211VirtualInterface::postSyncStateChanged`, checks role 6 at `ivars+88`, `len = 0`. |
| Manager helper event 46 (`postSyncStateChanged`) | `0xffffff80021e58b4` (site `0xffffff80021e594d`) | `0xffffff80020d3eac` (site `0xffffff80020d3ee0`) | `0xffffff80021823a8` (site `0xffffff80021823dc`) | yes | `IO80211AWDLPeerManager::postSyncStateChanged`, invokes the interface helper. |
| Event 47 emitter (`removePeer`) | `0xffffff8002254096` (site `0xffffff8002254248`) | `0xffffff800213a1a4` (site `0xffffff800213a351`) | `0xffffff80021e9d1c` (site `0xffffff80021e9ec9`) | yes | `IO80211PeerManager::removePeer`, direct `postMessageSync` with `esi = 0x2F`. |
| Event 47 emitter (`cleanUpForPeerRemovedFromNDP`) | `0xffffff8002214cf2` (site `0xffffff8002214d6d`) | `0xffffff80020ff5e8` (site `0xffffff80020ff65e`) | `0xffffff80021af406` (site `0xffffff80021af47c`) | yes | `IO80211AWDLPeerManager`, direct `postMessageSync` with `esi = 0x2F`. |
| Event 47 payload | 68 B | 68 B | 68 B | yes | MAC at `@0..5`, `u32@8 = 1`, rest 0 (peer-absence notification). |
| Event 48 emitter (`postAFToP2pd`) | `0xffffff800220fc2c` (site `0xffffff800220fce2`) | `0xffffff80020fa752` (site `0xffffff80020fa823`) | `0xffffff80021aa66c` (site `0xffffff80021aa71c`) | no | 26 calls virtual slot `+0xB18`; 14 and 15 call virtual slot `+0xB10`. |
| Event 48 payload | 72 B + frame | 72 B + frame | 72 B + frame | yes | 72 B `apple80211_act_frm_hdr` header concatenated with frame body. |
| Event 195 emitter (`setChannelSequenceUnconditional`) | `0xffffff80021cb69a` (site `0xffffff80021cc29b`) | `0xffffff80020bbee6` (site `0xffffff80020bc8a5`) | `0xffffff8002169b06` (site `0xffffff800216a627`) | no | 26 calls virtual slot `+0xB18`; 14 and 15 call virtual slot `+0xB10`. |
| Event 195 payload | 400 B | 400 B | 400 B | yes | `u32@0 = 1`, programmed channel sequence. |
| Chain step 1: `VirtualInterface::postMessage` | Slot 355 (`+0xB18`) `0xffffff8002333192` | Slot 354 (`+0xB10`) `0xffffff80021db9e4` | Slot 354 (`+0xB10`) `0xffffff80022a8c6e` | no | Slot shift in the `IO80211VirtualInterface` vtable. |
| Step 1 call site to Step 2 | `0xffffff800233334b` (`call [rax+0B20h]`) | `0xffffff80021dbb1c` (`call [rax+0B18h]`) | `0xffffff80022a8e27` (`call [rax+0B18h]`) | no | Calls the `reportDataPathEvents` virtual method. |
| Chain step 2: `SkywalkInterface::reportDataPathEvents` | Slot 356 (`+0xB20`) `0xffffff80022f8dc8` | Slot 355 (`+0xB18`) `0xffffff80021a3102` | Slot 355 (`+0xB18`) `0xffffff8002266276` | no | Slot in the `IO80211SkywalkInterface` vtable. |
| Step 2 call site to Step 3 | `0xffffff80022f8e2a` (`call [r10+850h]`) | `0xffffff80021a3157` (`call [r14+850h]`) | `0xffffff80022662d8` (`call [r10+850h]`) | yes | Invokes `executeCommand` through controller slot `+0x850`. |
| Chain step 3: `IONetworkController::executeCommand` | `0xffffff80026f51e0` | `0xffffff80025156ee` | `0xffffff8002630c98` | yes | Execution inside the serialized controller gate. |
| Chain step 4: `reportDataPathEventsGated` | `0xffffff80022f8d1a` | `0xffffff80021a306e` | `0xffffff80022661cc` | no | Callback with the controller gate taken. |
| Step 4 pre-filter | `call [rax+298h]` (`0xffffff80022f8d98`) | `call [rax+288h]` (`0xffffff80021a30d8`) | `call [rax+288h]` (`0xffffff8002266246`) | no | Filter slot shift (`+0x298` on 26, `+0x288` on 14 and 15). |
| Step 4 link to next step | `call postMessageInternal` (`0xffffff80022f8db2`) | `call postMessageSync` (`0xffffff80021a30f1`) | `call postMessageInternal` (`0xffffff8002266260`) | no | macOS 14 skips `postMessageInternal` and calls `postMessageSync` directly. |
| Chain step 5: `postMessageInternal` | `0xffffff80022f8600` (site `0xffffff80022f862a`) | Absent (not implemented) | `0xffffff8002265aba` (site `0xffffff8002265ae4`) | no | Link to `postMessageSync` with flag 0. On macOS 14 the function does not exist. |
| Chain step 6: `postMessageSync` | `0xffffff80022f8abc` | `0xffffff80021a2ce2` | `0xffffff8002265f6e` | yes | Synchronous delivery function to the user pipes. |
| Step 6 pipe check (`isIOUCPipeOpened`) | `call [rax+0DE0h]` (`0xffffff80022f8aed`) | `call [rax+0DE0h]` (`0xffffff80021a2d23`) | `call [rax+0DE0h]` (`0xffffff8002265f9f`) | yes | Slot `+0xDE0` in the `IO80211SkywalkInterface` vtable on the 3 versions. |
| Step 6 call to `postMessageIOUC` | `call [r9+0DD8h]` (`0xffffff80022f8b15`) | `call [r9+0DD8h]` (`0xffffff80021a2d5f`) | `call [r9+0DD8h]` (`0xffffff8002265fc7`) | yes | Slot `+0xDD8` in the `IO80211SkywalkInterface` vtable on the 3 versions. |
| Steps 7 and 8: `writePipe` filter and copy | `0xffffff80022a7e7c` | `0xffffff800217b97c` | `0xffffff8002239404` | yes | `IO80211IOUCEventPipe::writePipe` (on macOS 14 it does not receive the `int &` parameter). |

---

## 4. Action frame reception toward the daemon (`AWDL_AF_TO_DAEMON.md`)

| Item | 25G241 | 23J631 | 24H232 | Same (yes/no/?) | Note |
|---|---|---|---|---|---|
| `actionFrameInput` vtable slot | Slot 71 (`+0x238`) | Slot 71 (`+0x238`) | Slot 70 (`+0x230`) | no | `0xffffff800220e1ae` (26), `0xffffff80020f8a78` (14), `0xffffff80021a8bf4` (15). |
| AF ring element size | 104 B (`0x68`) | 104 B (`0x68`) | 104 B (`0x68`) | yes | Entry in the reception circular buffer. |
| Ring base pointer ivar | `ivars+16840` | `ivars+16808` | `ivars+16848` | no | Ring offsets in `IO80211AWDLPeerManager`. |
| Ring head ivar (`head`) | `ivars+16848` | `ivars+16816` | `ivars+16856` | no | Producer write index. |
| Ring tail ivar (`tail`) | `ivars+16852` | `ivars+16820` | `ivars+16860` | no | Consumer read index (process thread). |
| Ring size ivar (`size`) | `ivars+16856` | `ivars+16824` | `ivars+16864` | no | Maximum entry capacity. |
| Ring mask ivar (`mask`) | `ivars+16860` | `ivars+16828` | `ivars+16868` | no | Module mask (`size - 1`). |
| Overflow flag ivar | `ivars+16868` | `ivars+16836` | `ivars+16876` | no | Set to 1 when the AF queue fills. |
| AWDL enabled flag ivar (`AWDLOn`) | `ivars+16096` bit 1 | `ivars+16072` bit 1 | `ivars+16104` bit 1 | no | Checked before enqueueing into the ring. |
| Process function `actionFrameProcessInput` | `0xffffff800220e8b0` | `0xffffff80020f9318` | `0xffffff80021a9302` | yes | Core of action frame parsing and filtering. |
| AF validity filter (`isValidAF`) | `0xffffff800220fa64` | `0xffffff80020fa570` | `0xffffff80021aa4b0` | yes | Checks `hdrLen <= 0x47`, valid cursor and OUI. |
| OUI check (`actionFrameHasCorrectOUI`) | `0xffffff8002210200` | `0xffffff80020fad74` | `0xffffff80021aac34` | yes | `00:17:F2`, type 8, version != 0 on the 3 versions. |
| Known peer RSSI filter (`hdr+0x2C`) | `0xffffff800220ecb4` | `0xffffff80020f977e` | `0xffffff80021a9715` | yes | `cmp eax, [rcx+281Ch]` (`ivars+10268`, −70/−78 dBm). |
| Unknown peer RSSI filter (`hdr+0x2C`) | `0xffffff800220ecd6` | `0xffffff80020f97c0` | `0xffffff80021a9737` | yes | `cmp eax, [rcx+281Ch]` (`ivars+10268`, −70/−78 dBm). |
| Context error gate (`ctx+0x2C`) | `0xffffff800220ed36` | `0xffffff80020f9811` | `0xffffff80021a9797` | yes | `cmp dword ptr [r15+2Ch], 0`; if != 0 discards without event. |
| `peerReachability` field of the peer | `priv+0x78` | `priv+0x70` | `priv+0x78` | no | **−8 byte shift on macOS 14** (`priv = *(peer+0x20)`). |
| `peerReachability == 1` check | `0xffffff800220f930` (`[priv+78h]`) | `0xffffff80020fa42e` (`[priv+70h]`) | `0xffffff80021aa36d` (`[priv+78h]`) | no | Mandatory condition before the publish call. |
| `postAFToP2pd` call site | `0xffffff800220f94c` | `0xffffff80020fa445` | `0xffffff80021aa384` | yes | Only `postAFToP2pd` call in the whole kernelcache. |
| `postAFToP2pd` function head | `0xffffff800220fc2c` | `0xffffff80020fa752` | `0xffffff80021aa66c` | yes | Buffer builder and event 48 publisher. |
| Virtual publish in `postAFToP2pd` | `0xffffff800220fce2` (`+0B18h`) | `0xffffff80020fa823` (`+0B10h`) | `0xffffff80021aa71c` (`+0B10h`) | no | 26 calls slot 355; 14 and 15 call slot 354. |

---

## 5. PSF and MIF template emission (`AWDL_PSF_MIF_EMISSION.md`)

| Item | 25G241 | 23J631 | 24H232 | Same (yes/no/?) | Note |
|---|---|---|---|---|---|
| PSF template builder (`updateSyncFrameTemplate`) | `0xffffff80021dc5b2` | `0xffffff80020cb764` | `0xffffff8002179b0e` | yes | Builds the periodic sync frame template. |
| PSF buffer initialization (512 B, `0xAA`) | `0xffffff80021dc5f1` (site `0xffffff80021dc5f6`) | `0xffffff80020cb7a3` (site `0xffffff80020cb7a8`) | `0xffffff8002179b4d` (site `0xffffff8002179b52`) | yes | `secure_memset(..., 0xAA, 0x200)` on the 3 versions. |
| Ioctl 151 load (`0x97802869C8`) | `0xffffff80021dc97b` | `0xffffff80020cbb98` | `0xffffff8002179ed1` | yes | `mov rax, 97802869C8h` on the 3 versions. |
| `ioctl_internal` call on 151 | `0xffffff80021dc996` | `0xffffff80020cbbb3` | `0xffffff8002179eec` | yes | Delivers the `awdl_sync_frame_template_req` structure (16 B). |
| TLVs stacked in PSF template | 11 TLVs (6, 24, 12, 7, 30, RTG, 17, 21, 20, 23, 33) | 11 TLVs (6, 24, 12, 7, 30, RTG, 17 inlined, 21, 20, 23, 33) | 11 TLVs (6, 24, 12, 7, 30, RTG, 17, 21, 20, 23, 33) | yes | TLVs 4, 5 and 18 absent (must be added by the driver). |
| MI template builder (`buildMasterIndicationTemplate`) | `0xffffff8002211b42` | `0xffffff80020fc59c` | `0xffffff80021ac4c6` | yes | Builds the Master Indication template (1200 B). |
| MI template ivar buffer (1200 B) | `ivars+9048` | `ivars+9048` | `ivars+9048` | yes | Identical on the 3 versions. |
| MI template length ivar (`template_len`) | `ivars+10248` | `ivars+10248` | `ivars+10248` | yes | Identical on the 3 versions. |
| ARPA IPv6 descriptor pointer (`_rarpv6Service`) | `ivars+42448` (`0xA5D0`) | `ivars+32360` (`0x7E68`) | `ivars+32432` (`0x7EB0`) | no | Variable offset of `_rarpv6Service` in ivars. |
| TLV 16 ARPA type write in MI | `0xffffff800221202d` (`mov byte [rsi-4], 10h`) | `0xffffff80020fca8f` (`mov byte [rsi-4], 10h`) | `0xffffff80021ac98d` (`mov byte [rsi-4], 10h`) | yes | Direct insertion of the TLV 16 header into the MI template. |
| TLV 16 ARPA data copy (`copyDataOnly`) | `0xffffff8002212053` | `0xffffff80020fcab3` | `0xffffff80021ac9b1` | yes | `IO80211ServiceRequestDescriptor::copyDataOnly(uchar *, ulong)`. |
| Unicast UMI emitter (`sendUnicastMI`) | `0xffffff8002213c0a` | `0xffffff80020fe74c` | `0xffffff80021ae404` | yes | Generates a unicast transmission request for a peer. |
| Ioctl 149 load (`0x95802869C8`) in UMI | `0xffffff8002213f22` | `0xffffff80020fe98a` | `0xffffff80021ae6eb` | yes | `mov rax, 95802869C8h` on the 3 versions. |
| `ioctl_internal` call in UMI | `0xffffff8002213f32` | `0xffffff80020fe99a` | `0xffffff80021ae6fb` | yes | 56 B `awdl_mi_request` structure with peer MAC. |
| MI broadcast emitter (`updatePrimaryPayloadMI`) | `0xffffff80021fb36a` | `0xffffff80020e7806` | `0xffffff8002196df2` | yes | Generates immediate transmission (149) or firmware periodic (225). |
| Ioctl 149 load ("send now") in primary MI | `0xffffff80021fb533` (`mov [var+4], 95h`) | `0xffffff80020e79c0` (`mov [var+4], 95h`) | `0xffffff8002196fb5` (`mov [var+4], 95h`) | yes | Immediate MIF injection into the current window. |
| Ioctl 225 load ("template to fw") in primary MI | `0xffffff80021fb546` (`mov [var+4], 0E1h`) | `0xffffff80020e79cc` (`mov [var+4], 0E1h`) | `0xffffff8002196fc8` (`mov [var+4], 0E1h`) | yes | Periodic scheduling per availability window. |
| Secondary MI emitter (`updateSecondaryPayloadMI`) | `0xffffff80022141ae` | `0xffffff80020feb3e` | `0xffffff80021ae978` | yes | Generates the secondary template for SDB operation. |
| Ioctl 342 load (`0x156802869C8`) in secondary MI | `0xffffff8002214304` | `0xffffff80020fec94` | `0xffffff80021aeace` | yes | `mov rax, 156802869C8h` on the 3 versions. |
| `ioctl_internal` call in secondary MI | `0xffffff8002214314` | `0xffffff80020feca4` | `0xffffff80021aeade` | yes | 16 B `awdl_sec_mi_request` structure over shared buffer. |

---

## 6. What changes for a driver layer that must support macOS 14, 15 and 26

For the AWDL compatibility layer in `RTL8814Native.kext` / `RTL8814AirLayer.kext` to work
unified on macOS 14.8.9, 15.8.2 and 26.7.1, the following divergences must be resolved
dynamically:

### 6.1 Dynamic dispatch of virtual methods
The vtable slots are not stable across the three OS branches. The driver layer must not compile
calls with fixed offsets or fixed vtable macros, but resolve the slots according to the running
kernel version (`version_major`):

1. **`postMessage` in `IO80211VirtualInterface`**:
   - macOS 14 (Darwin 23) and macOS 15 (Darwin 24): slot 354 (`+0x0B10`).
   - macOS 26 (Darwin 25): slot 355 (`+0x0B18`).
2. **`actionFrameInput` in `IO80211AWDLPeerManager`**:
   - macOS 14 (Darwin 23) and macOS 26 (Darwin 25): slot 71 (`+0x0238`).
   - macOS 15 (Darwin 24): slot 70 (`+0x0230`).
3. **`reportDataPathEvents` in `IO80211AWDLPeerManager`**:
   - macOS 14 (Darwin 23) and macOS 15 (Darwin 24): slot 81 (`+0x0288`).
   - macOS 26 (Darwin 25): slot 83 (`+0x0298`).
4. **`dequeueTxPacket` in `IO80211AWDLPeerManager`**:
   - macOS 14 (Darwin 23): slot 84 (`+0x02A0`).
   - macOS 15 (Darwin 24): slot 83 (`+0x0298`).
   - macOS 26 (Darwin 25): slot 85 (`+0x02A8`).

### 6.2 Dynamic offset of `peerReachability`
In `IO80211AWDLPeer`, the `peerReachability` field determines whether received action frames are
published to `wifip2pd` via event 48. If the driver needs to inspect or validate the peer state
from `priv = *(peer+0x20)`:
- On macOS 14 (Darwin 23): `*(uint32_t*)(priv + 0x70)`.
- On macOS 15 (Darwin 24) and macOS 26 (Darwin 25): `*(uint32_t*)(priv + 0x78)`.

### 6.3 Instance variable offsets in `IO80211AWDLPeerManager`
The size and layout of the private ivars of `IO80211AWDLPeerManager` vary noticeably:
- **Action Frame reception ring (104 B per entry)**:
  - Base pointer: `+16808` (14), `+16848` (15), `+16840` (26).
  - Head: `+16816` (14), `+16856` (15), `+16848` (26).
  - Tail: `+16820` (14), `+16860` (15), `+16852` (26).
  - Capacity / Size: `+16824` (14), `+16864` (15), `+16856` (26).
  - Mask: `+16828` (14), `+16868` (15), `+16860` (26).
  - Overflow flag: `+16836` (14), `+16876` (15), `+16868` (26).
- **`AWDLOn` flag**: `+16072` bit 1 (14), `+16104` bit 1 (15), `+16096` bit 1 (26).
- **ARPA IPv6 descriptor (`_rarpv6Service`)**: `+32360` (14), `+32432` (15), `+42448` (26).
- **Host start timestamp**: `+32600` (14), `+32680` (15), `+42696` (26).
- **Slot 0 gap threshold**: `+51392` (14), `+52016` (15), `+66376` (26).
- **Window extension counter**: invariant at `ivars+408` on the three versions.
- **`inAvailabilityWindow` flag**: invariant at `ivars+772` on the three versions.

### 6.4 Event emission and the synchronous path
On macOS 14, `IO80211SkywalkInterface::postMessageInternal` does not exist. The
`reportDataPathEventsGated` method jumps directly to `postMessageSync`. If the driver layer
implements event injection or simulation via virtual calls on the interface:
- Invoking the `postMessage` virtual slot (`+0xB10` on 14/15, `+0xB18` on 26) guarantees that on
  all versions the controller gate is taken (`IONetworkController::executeCommand` at `+0x850`)
  and the synchronous delivery to the user event pipe completes without depending on
  unexported symbols.

---

## 7. Open points

1. `?` **Hot-path behavior of the `executeCommand` gate**: On the three versions, step 3 of the
   `postMessage` chain goes through `IONetworkController::executeCommand`, acquiring the
   controller gate. Still to be confirmed experimentally on hardware whether invoking this path
   from a deferred interrupt service routine (secondary workloop or USB callback) introduces
   41/43 event scheduling latencies incompatible with the required precision (≤ 3 TU).
2. `?` **Use of inlined TLV 17 on macOS 14.8.9**: In `updateSyncFrameTemplate`, macOS 14 codes
   the construction of the 802.11 container TLV 17 completely inlined instead of invoking
   `buildIEEE80211CntnrTlv` as macOS 15 and 26 do. Still to validate whether any internal field
   of the sub-TLV varies when operating in real SDB mode.
3. `?` **Consumption of events 41/42/43 by auxiliary components**: On none of the three analyzed
   versions does the daemon `wifip2pd` consume events 41, 42 or 43. It has not been examined
   whether complementary daemons such as `wifivelocityd` or `airportd` on macOS 14/15
   passively subscribe to these events for telemetry.
