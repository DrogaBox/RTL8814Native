# Capability 29 (dual band): who else reads it before enabling it

The test build of the AirLayer `3699d28e` adds `p[7] |= 0x20` in
`RTLAirController::getCARD_CAPABILITIES` (capability 29, kernel byte `+0xB37`, bit `0x20`;
in userspace, after removing the 4-byte version, byte 3 bit `0x20`). Without that bit, the AWDL
manager leaves the sequence only on channel 6 (`AWDL_SECUENCIA_CON_INFRA.md` §4.2). This document
records what else consumes the bit on 25G241, to know whether it can have side effects like those
of capability 75 (SAE), which changed the security negotiation with the transition router.
Unverified items are marked `?`.

## 1. Kernel

All the readers of `+0xB37 & 0x20` in the kernel collection belong to the AWDL manager
(`AWDL_CARD_CAPS_RESTANTES.md` §`+0xB37`, verified in IDA):

| Reader | VA | Effect with the bit |
|---|---|---|
| `IO80211AWDLPeerManager::configureValidAWDLChannels` | `0xffffff80021cab1e` | keeps the 5 GHz social channels (149/44) in `+0x4294` |
| `IO80211AWDLPeerManager::handleAssistedDiscoveryTrafficReg` | `0xffffff8002205f0c` | allows a 5 GHz social channel for assisted discovery |
| `IO80211AWDLPeerManager::fillLegacyDataPathPayload` | `0xffffff80022143fb` | base flags of TLV 12: `0x8B23` instead of `0x8B03` |

No reader is in the infrastructure path (`IO80211InfraInterface`, association, roaming,
scanning). The generic accessor `IO80211Controller::isCardCapabilitiesSet` has no direct callers.

## 2. Userspace

- **airportd** (`/usr/libexec/airportd`): a sweep of the disassembly looking for tests
  `[x+7], 20h` (the same shift convention with which the sweep does find the
  AWDL gate of wifip2pd at `[x+8], 1`) found no candidates. The sweep is textual: a
  test done on the byte already loaded into a register would not show up `?`. Its "DualBand"
  strings (`__defaultDualBandSingleAPRoamingProfile_*`, `CWRoamParamsTypeDualBand`, "Forcing dual-band
  roam profile because BT audio is in use") describe roaming profiles depending on the access point
  environment, with no demonstrated relation to the card capability `?`.
- **CoreWiFi**: there are no tests of that bit either in the image extracted from the shared cache;
  `supports5GHzNetworks` is a property of each scan result (`-[CWFScanResult
  supports5GHzNetworks]`), not of the card.
- **wifip2pd** (`/usr/libexec/wifip2pd`): the only tests over the capabilities copy are the
  AWDL gate (`sub_100047660`, `0x1000483b5`, byte 4 bit 0) and two bit 0 tests in
  `sub_1002E5440`/`sub_1002E5570`. Its `supportsDualBand` is a field of the
  Wi-Fi Aware (NAN) interface information: the initializers that use it
  (`initWithInterfaceName:…:supportsDualBand:…`, references in `sub_1002E9470` and
  `sub_1003C3C40`) assemble Wi-Fi Aware services (`WiFiAwarePublishServiceSpecificInfo`). With NAN
  off due to capability 72, that path does not run. Where it takes the value from was not followed `?`.

### 2.1 Queries by capability number

`airportd` also queries capabilities by value: `-[CWInterface capabilities]` returns the
set of numbers and the caller tests `containsObject:` with an NSNumber literal. That is how the
SAE gate of `-[CWXPCInterfaceContext __allowJoinCandidate:trigger:reply:]` works (`0x1000bbf66`):
`capabilities` at `0x1000bbfc4`, `containsObject:` with the literal 75 from `__objc_intobj`
(`0x10015df00`, referenced from `0x1000bbfd8`).

- **airportd**: the `__objc_intobj` section (`0x10015d618`–`0x10015e938`) contains no
  literal 29, and none of the 28 functions that call the `capabilities` selector use an
  immediate 29. The numbers those functions query are 0, 20, 32, 50, 75, 81, 88, 90, 94,
  100, 105, 107, 109, 113, 144, 146 and 165. Nobody asks for 29.
- **wifip2pd**: it has no `__objc_intobj` section or references to the `capabilities` selector; it only
  uses the bit tests of §2.
- CoreWLAN, CoreWiFi and other clients of `-[CWInterface capabilities]` (sharingd, menu bar
  agents) were not swept by value: the CoreWiFi image carved from the shared cache does not
  preserve functions well `?`.

## 3. Verdict

With what was found, capability 29 only changes AWDL behavior: 5 GHz social channels in the
sequence, assisted discovery and one bit of TLV 12. No consumer appears in the
association, scanning or roaming path of the infrastructure, unlike capability 75, which
`airportd` uses to choose security. Moreover the bit is truthful (the 8814AU is dual band), and
every Mac with a 5 GHz radio announces it.

Remaining risk: a userspace consumer outside `airportd` and `wifip2pd` asking for
29 by value (§2.1) `?`. If
after booting with `3699d28e` the association to 153/80 changes, the way back is reinstalling
`65266e39`.

## 4. Result on hardware (2026-10-31, boot with `3699d28e`)

- Layer loaded: `com.DrogaBox.driver.RTL8814AirLayer` 0.32.0, UUID
  `FFB1C931-4E66-3F26-9932-296479EAC642`, the same as binary `3699d28e`.
- Infrastructure: normal association on the first attempt; the router now announces primary
  157 (same 80 MHz block 149–161 as the 153 of previous documents). With AWDL active,
  20 of 20 pings to the gateway, mean 0.62 ms, maximum 0.69 ms. No change observed in association,
  security or roaming, unlike capability 75.
- At rest only the initial 129 `6 0 0 0 0 0 0 0 6 0 0 0 0 0 0 0` arrives, same as without the bit.
  When opening the Finder AirDrop window the manager rebuilds the sequence: the social 149
  appears for the first time and the infrastructure slots resolve to 157.

| # | Sequence of the 129, slots 0–15 |
|---:|---|
| 1 | `149 149 149 149 149 0 0 0 6 149 149 149 149 0 0 0` |
| 2 | `149 149 149 149 149 149 157 157 6 149 149 149 149 149 157 157` |
| 3 | `149 149 149 0 0 0 0 0 6 149 149 0 0 0 0 0` |
| 4 | `157 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0` |

- None matches the Discovery row of `AWDL_SECUENCIA_CON_INFRA.md` §6
  (`3 3 3 3 1 1 7 7 2 4 3 4 1 1 7 7`): in 1 and 2 slots 4 and 12 (type 1, "unserved"
  in that table) carry 149, and 4 puts 157 in slot 0. Which state and which row
  of the table produces each one remains open `?`. Number 2 has 157 in the type 7 slots,
  which per §5.2 of that document requires at least one registered AirPlay service `?`.
- Driver per-slot switching: to 149 in 4357 and 4355 µs, back to the BSS in 2389 and
  2415 µs, within a 64 TU slot (65.5 ms). First data frame over `awdl0`:
  IPv6 mDNS multicast to `33:33:00:00:00:fb`, 179 B.
- First TX summary (at 1024 PSF, channel 149): 1024 PSF and 731 MIF with no failures in 744
  windows; data 11 enqueued, 6 sent, 5 dropped by age, none by full queue
  nor by error; 1464 switchings without failure, maximum 5407 µs, mean 3381 µs.
- Verdict: bit 29 was the missing gate for the 5 GHz social channel; the risk of §3 did not
  materialize in the infrastructure.
