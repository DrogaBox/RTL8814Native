# Capability 78 ("Concurrency supported"): SoftAP plus AWDL, not infrastructure

`AWDL_CARD_CAPS_RESTANTES.md` left capability 78 open (`p[13]` bit `0x40`, kernel byte
`+0xB3D`): `IO80211AWDLPeerManager::configure` reads it at `0xffffff80021c58b3`, logs
"Concurrency supported" and sets byte `state+0xD066` to 1 (write at `0xffffff80021c58ec`;
`initParamsToDefaults` sets it back to 0 at `0xffffff80021c7f45`). The question was whether that
concurrency is the one of AWDL with an infrastructure association, and therefore whether the layer
should announce it for the `rtlawdl_mcc` mode. Static analysis of the 25G241 kernel collection.

## 1. Readers of `state+0xD066`

Only two functions read it, both about the software access point (SoftAP / hotspot):

- `IO80211AWDLPeerManager::validateForHostapAWDLConcurrency` (`0xffffff80021d8a54`, test at
  `0xffffff80021d8a80`). It is called by `enableAWDL` (`0xffffff80021dd37d`), `exitQuietMode`
  (`0xffffff80021fae5b`), `setSoftApCSANanPh` (`0xffffff8002219f95`) and
  `IO80211P2PSupervisor::requestNANEnable` (`0xffffff8002391a1c`). With the bit set and an active SoftAP
  (`state+0xD06C` different from 0), if the SoftAP channel (`state+0xD070`) is not the chosen
  social channel (the configured 5 GHz one, else 149, else 44, else the master channel), it orders a
  SoftAP channel switch with `hostapForceChannelSwitch(channel, 2)` and logs "Sending CSA for channel
  %u". If that CSA fails, it returns 0 with "CSA on SoftAP failed. Failure to enable AWDL". With the bit
  clear that whole branch is skipped, it keeps the SoftAP channel and returns 1.
- `IO80211AWDLPeerManager::notifyHostapState` (`0xffffff80021fec60`, test at
  `0xffffff80021fec6e`; no direct callers, entered via the vtable): with the bit set it copies the
  SoftAP state and channel into `state+0xD06C/0xD070/0xD072`, turns AWDL off in a migration if it was
  already on and calls `setHostApAWDLConcurrency(3 or 8)`.

Neither touches the infrastructure interface, the channel sequence with the infra channel, the
availability windows nor the election. The relation between AWDL and the client association
comes out of `getInfrastructureState` (`AWDL_SECUENCIA_CON_INFRA.md`), not from this capability.

## 2. Verdict for the RTL layer

Capability 78 describes a radio that can keep a SoftAP and AWDL at the same time, moving the
SoftAP to the social channel with a CSA. The 8814AU with this driver offers no SoftAP to IO80211 nor
implements the SoftAP CSA, so announcing it would only open the path that ends in "Failure to
enable AWDL" if a SoftAP ever existed. For time sharing with the router network
(`rtlawdl_mcc`) it contributes nothing: it must stay clear, as today.

`state+0x103A0` and `state+0x102F1` (bits 2 and 3 of `p[24]`) and `sub+0x6FC` (dynamic country
code) remain open `?`.
