# Triggers of presence publication in `wifip2pd` (ioctl 415)

Which peer event makes the daemon publish the presence of an AWDL peer and release its PPP queue
with ioctl 415 (`APPLE80211_IOC_POST_PRESENCE_DONE`). Complements
[AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md)
§2, which located the two emitters of the 415 and left open what triggers each path. Closes
pending point nº 1 of its §6. Base: `wifip2pd` of 25G241 (binary md5
`a120cfccfa2783303604ce5be9e1763a`).
Binary addresses unless noted. Anything unchecked carries `?`.

## 1. Short answer

The primary trigger is the arrival of an **AWDL action frame from the peer**: the kernel delivers
it to
the daemon as event 48 (`AWDL_ACTION_FRAME_RX`), the daemon decodes it as `DriverEvent.AWDL`
tag 1 and publishes the transmitter's presence **only once per peer** (own flag of the peer).
Three secondary paths register or re-publish presence within the data flow: datapath
established on the publisher side, datapath response on the initiator side, and traffic registration
(set and recovery). The four converge on the neighbor table method that calls the protocol method
sending the 415.

## 2. The neighbor table and the publish method

`sub_1002823D0` is a method of `CoreP2P.WiFiInterface.NeighborDiscoveryTable` (type metadata at
`0x100287ac0`; that of `NeighborDiscoveryTable.Entry` at `0x100287bb0`, mangled symbol
`$s7CoreP2P13WiFiInterfaceC22NeighborDiscoveryTableV5EntryVMa`). The table is a dictionary
`MAC → Int?` (generic getter `_$sSDyq_Sgxcig` at `0x100444b3e`; setter `_$sSDyq_Sgxcis` at
`0x100444b44`; `Dictionary.removeValue(forKey:)` at `0x100444ae4`; `_NativeDictionary._insert` at
`0x100445204`; own slot search `sub_1002875B0`, called from `0x100282807` and `0x100282873`).

- **Presence publication** (`sub_1002823D0`): if the MAC **has no entry** (outer tag
  of the `Int??` returned by the getter equal to 1), it calls witness 17 of the protocol
  conformance — the method sending the 415, `sub_1000CA0F0` with send at `0x1000ca412` or
  `sub_1000D6000` with send at `0x1000d6229` (AWDL_PEER_PRESENCE_PPP.md §2) — and inserts the
  entry with zero/null value
  (`0x10028293d`). Success log: `"Posted peer presence for %s on %s"` (format at `0x10049a020`,
  cited at `0x10028272e`, `_os_log_impl` at `0x100282748`). If the entry exists, it does not
  publish and only guarantees it stays present. With the 6 B MAC copied at
  `0x100282574`-`0x1002825a0` from the first argument `?` (read from the pseudocode).
- **Absence publication** (`sub_100282980`): with no entry or value ≤ 0 it does nothing; with
  value equal to 1 it deletes the key (`removeValue(forKey:)` at `0x100282bef`), calls witness 18
  and logs `"Posted peer absence for %s on %s"` (cited at `0x100282d99`); with value > 1 it
  decrements it with the setter (`0x100282b79`). The `Int?` works as a reference counter `?`
  (none of the traced paths writes positive counts; see §7).
- **Full wipe** (`sub_100282E30`, `Dictionary.removeAll`): invoked by interface creations
  (`0x1003ab880`, `0x1003ac420`, `0x1003aeee0` — log `"Failed to initially clear the peer presence
  table because %@"` at `0x1003b13cf` —, `0x1004158a0`, `0x100416d00`, `0x10041b1f0`) and by the
  driver event dispatcher `sub_1004184D0`.

## 3. The four paths

### 3.1 AWDL action frames (event 48): the main path

Chain: `sub_1004182B0` (entry of the driver event handler; log `"Failed to dispatch event
%s because the AWDLInterface was deallocated before the event was received from the driver"` at
`0x100418449`) → `sub_1004184D0`, which switches over the `DriverEvent` enum with
`swift_getEnumCaseMultiPayload`.

- Case 1 of `DriverEvent` = `DriverEvent.AWDL`. Inside, case 1 of `DriverEvent.AWDL`: it decodes a
  `CoreP2P.AWDLActionFrame.Header` (metadata `$s7CoreP2P15AWDLActionFrameV6HeaderVMa`, cited at
  `0x10041a229`) with `sub_100031330`; if the result is success (`and eax, 0C0000000h` /
  `cmp eax, 40000000h` at `0x10041a1f0`–`0x10041a1f5`), it calls `sub_10041C470` at `0x10041a216`.
  Case 0 of `DriverEvent.AWDL` is another event (log `"Realtime mode updated %{bool}d"` at
  `0x100418a9f`); cases 2 and following were not traced `?`.
- It corresponds to the kernel's event 48: the event table of `AWDL_ETAPAS_2_A_4.md` maps 48
  (`len >= 0x48`, 72 B `apple80211_act_frm_hdr` header) to `DriverEvent.AWDL` tag 1.

`sub_10041C470` (`0x10041c470`), with the AF header:

1. Two guards over `self` (the interface): the byte of the ivar whose position the entry 760 of
   the ivar offset table gives must be non-zero, and the one of entry 712 must be 0
   (`sub_10041C470`, guard lines in the pseudocode; names of the Bools unidentified `?`).
2. Statistics hook `sub_10040F120` with a 4 B field of the header (2 + 2 bytes).
3. It locates or builds the transmitter's `AWDLPeer` (box with the type metadata; 6 B MAC with
   `sub_100172140`).
4. **Publication condition**: the Bool of the peer struct (field read with the metadata offset, in
   a box) equal to 0. Then it publishes via `sub_1002823D0` (called at `0x10041cc69`) and
   sets the Bool to 1 (the write is not located `?`); on failure it logs `"Failed to set the peer
   presence for %s: %@"` (format at
   `0x1004a7f90`, cited at `0x10041cd97`). If it was already 1, it does not repeat the publication:
   that is why each AF of a known peer generates no new 415.
5. Then it calls `sub_10041FD50` (traffic registration recovery, which ends in §3.4) and,
   if the AF carries an `AWDLActionFrame.DataPathState` whose byte at `+64` differs from 49 (0x31),
   it looks up the traffic registration configuration (`sub_10014B180` with the
   `AWDLTrafficRegistrationConfiguration` map of `sub_100068EA0`) and activates it with the
   interface witness with arguments (1, 1). Exact semantics of the byte and of 0x31 unclosed `?`.

### 3.2 Datapath established, publisher side

`sub_10028F280` is the coroutine of the publisher's datapath manager (no direct callers: it is an
async function; its resume-function table lives at `0x10052e680`–`0x10052e6c0`, referenced by
`sub_10028F280` at `0x100293941` and `sub_100299AD0` at `0x10029a832`, and the resume
`sub_100298610` at `0x10052e6a8` is who takes it up). Cycle strings: `"%@ received a new
datapath request from %s"` at `0x10028ffbc`, `"%@ datapath established with %s"` at
`0x100290237`, `"%@ received datapath confirmation with status %s"` at `0x100290cdb` and the
selector
`publishDataConfirmedForHandle:localInterfaceIndex:serviceSpecificInfo:` `?` (VA of the
`objc_msgSend` not captured). It publishes at two sites:

| Site | Context |
|---|---|
| `0x10029148e` | after datapath establishment |
| `0x1002943f5` | after data publish confirmation |

Failure: `"Failed to post peer presence for %s because %@"` (format at `0x10049a960`, cited at
`0x1002915bc`). No prior guard was seen at either site `?`.

### 3.3 Datapath response, initiator side

`sub_1002FF710` is the initiator's datapath manager (`"%@ received datapath response with status
%s from responder data address: %s [NMI: %s]"` at `0x10030305c`; `"NAN-DPResp: Peer %s, Self
gtk_required: %{bool}d, Peer gtk_required: %{bool}d"` at `0x10030344b` and `0x100303637`). During
the response processing it calls `sub_100305990` at `0x100302802` after building the MAC
(`sub_1003047A0`).

`sub_100305990` (`0x100305990`) operates on the self's `InitiatorInstance` (metadata
`$s17InitiatorInstanceC12PeerIdentityVMa`, called at `0x1003059c3`); the peer identity —6 B MAC
followed by a Bool at `+6`— lives in the ivar whose offset it takes from the class at `+0xA0` of
the offset table. **Condition**: if the Bool at `+6` is 1 (cmp at `0x100305aa7`), presence was
already published and it returns; if not, it publishes (calls to `sub_1002823D0` at
`0x100305bc9` and `0x1003063c4`), writes 1 to a session-own Bool and logs the failure with the
same format `0x10049a960`
(cited at `0x100305cef`).

### 3.4 Traffic registration

The executor is `sub_1002A7790` (`"Successfully %s traffic registration for %s to %s"` at
`0x10049b300`/`0x1002a7fa9`; `"Successfully updated traffic registration options for %s to %s"` at
`0x10049b380`/`0x1002a7aa0`; `"Failed to %s traffic registration for %s to %s because %@"` at
`0x10049b340`/`0x1002a7d32`; called by `sub_1002B63F0`, in turn from `sub_1002A6FF0`). With a
*set* type operation (byte equal to 1) and with the service class check
`sub_1002A2630` other than 0, it calls `sub_10040B650` at `0x1002a7923`; with a delete operation
it goes to `sub_10040CD80` (absence, §5).

`sub_10040B650` (`0x10040b650`):

- **Condition**: the `uniqueIdentifier` of the registration service is not
  `AWDLTrafficRegistrationServiceAirPlayConnectivity` (two comparisons against the global
  constant of the same name cause early return) and its class, per `sub_10014A980`, is 2. The
  semantics of class 2 (it seems to correspond to AWDL services) unclosed `?`.
- It publishes via `sub_1002823D0` at `0x10040c454`. Strings: `"Bypass conflict check traffic
  registration %s to %s for watchdog recovery"` at `0x1004a6bb0`/`0x10040bff6`; `"EdgeKit is not
  supported"` at `0x1004a6b90`/`0x10040c298`; if the publication fails, it undoes it with `"Will
  clear
  traffic registration for %s to %s because failed to post peer presence on %s: %@"` at
  `0x1004a6b30`/`0x10040c63b`.
- Two other callers: `sub_100410830` (call at `0x1004108dd`; context string
  `"P2P Controller.queryNANDataPathState for XPC Session: %@"` at `0x10036930b`, via
  `sub_100368C60` ← `sub_10036E130` — P2P Controller XPC queries `?`) and `sub_10042A000`
  (call at `0x10042a3f4`; `"Attempting to recover traffic registration for %s to %s"` at
  `0x1004a82c0`/`0x10042a33d` and `"Successfully recovered traffic registration for %s to %s"` at
  `0x1004a8280`/`0x10042a7c2`; its caller `sub_100444050` is reachable from `sub_10041FD50`, invoked in
  §3.1 step 5 after every action frame).

## 4. Summary

| Path | Trigger | Condition | Call to `sub_1002823D0` | Own log (format) |
|---|---|---|---|---|
| §3.1 | Event 48 (AWDL AF) → `DriverEvent.AWDL` tag 1 → correct `AWDLActionFrame.Header` | 2 interface Bools (ivars 712/760) and the "posted" Bool of `AWDLPeer` equal to 0 | `0x10041cc69` | `"Failed to set the peer presence for %s: %@"` `0x1004a7f90` |
| §3.2 | Datapath established / data published, publisher side | none seen `?` | `0x10029148e`, `0x1002943f5` | `"Failed to post peer presence for %s because %@"` `0x10049a960` |
| §3.3 | Datapath response (DPResp), initiator side | `PeerIdentity+6` other than 1 (`0x100305aa7`) | `0x100305bc9`, `0x1003063c4` | `"Failed to post peer presence for %s because %@"` `0x10049a960` |
| §3.4 | Traffic registration SET and recovery (also after every AF, §3.1) | service class 2 and not AirPlayConnectivity | `0x10040c454` | `"Bypass conflict check traffic registration %s to %s for watchdog recovery"` `0x1004a6bb0` |

The success log is always emitted by the neighbor table: `"Posted peer presence for %s on %s"`
(`0x10049a020`). The two emitters of the 415 additionally log `"Post peer presence for %s"`
(`0x100495b30`) at `0x1000ca2fa` and `0x1000d615c`.

## 5. The absence, for contrast

`sub_100282980` (§2) is triggered by: `sub_100298E00` (datapath close on the publisher side;
`"Failed to post peer absence for %s because %@"` at `0x1002992e1`), `sub_10030D630`
(initiator side; at `0x10030db93`), `sub_10040CD80` (deletion of a traffic registration;
`"Failed to clear peer %s on %s: %@"` at `0x10040d267`, with the same service class 2 as
condition) and `sub_100427D70` (expiry by inactivity: `"Aged out %s with total traffic
registrations: %ld, total responder traffic registrations: %ld"` at `0x100428026` and
`"Failed to post peer absence for %s: %@"` at `0x100428294`).

## 6. Implications for the RTL8814 driver

1. The whole flow depends on event 48: without AFs delivered to the daemon there is no presence,
   no 415, no release of the PPP queue of `IO80211AWDLPeer`. Consistent with the plan order: RX
   diversion and AF injection (steps 2–3) before anything.
2. Publication repeats per path: once per peer per action frame, plus one per datapath session and
   one per traffic registration. The kernel tolerates it because each 415 only releases the
   PPP queue of the peer (`setAwdlPostPeerPresenceDone` `0xffffff800221c978` →
   `pppDone(true)`).
   `pppDone(true)` only walks the queue if it is not empty, and after the first 415 the flag
   `+1758` of
   the peer is already 1, so `IO80211AWDLPeer::skywalkInputPacket` stops holding packets
   ([AWDL_PEER_PRESENCE_PPP.md](AWDL_PEER_PRESENCE_PPP.md) §2 and §3). Repeated 415s deliver
   nothing new.
3. The once-per-peer publication condition in §3.1 (Bool in the peer struct) is daemon state: the
   driver must not assume one 415 per AF.

## 7. Pending

- Names of the two interface Bools (entries 712 and 760 of the ivar offset table of the
  class of §3.1) and of the "presence posted" flag of the `AWDLPeer` struct `?`.
- Who writes positive counts into the NDP dictionary (the ~33 users of the generic setter
  `0x100444b44` include `sub_10040AEC0`, which touches a dictionary of
  `AWDLTrafficRegistrationConfiguration`/`P2PTimer` per UUID in the interface ivars 616/632/864,
  not the NDP) `?`.
- Detail of the byte of `AWDLActionFrame.DataPathState` that activates the traffic registration
  and why the value 49 excludes it `?`.
- Which AFs reach the daemon as event 48 (UMI condition of subtype + channel): still open in
  `AWDL_ETAPAS_2_A_4.md` (point E3).
