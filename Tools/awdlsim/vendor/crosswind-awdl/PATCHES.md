# Local changes to crosswind-awdl 406272f

Vendored from https://github.com/Mr-Hafid/Crosswind, `crates/crosswind-awdl` at `406272f` (MIT, see
`LICENSE`), the same code as t4t5/owly `owly-awdl` at `9a676e1` apart from names.

- `Config::metric: Option<u32>` (`src/node.rs`): when set, `Node::new` starts its election with that
  self and master metric instead of `METRIC_INIT` (60). `Config::new` leaves it `None`.
- `Config::counter: Option<u32>` (`src/node.rs`): the same for the election counter, which otherwise
  stays at `COUNTER_INIT` (0); AWDL that ranks by counter before metric needs it to pick a master.
- `put_ht_capabilities` (`src/tlv.rs`): the HT Capabilities TLV carries two zero bytes and then the whole
  26-byte body of the 802.11 HT Capabilities element (28 bytes), not legacy's 8. macOS 26.7.1
  (`IO80211Peer::parseHTCapTLV`, called from `IO80211AWDLPeer::actionFrameReport` and
  `doMinimalAFProcessing` with the value from byte 2) takes the element only at exactly 26 bytes; without it
  `hasHTorVHTCaps` is false, `IO80211P2PDataPathManager::addPeerToFWCache` never caches the peer and
  `IO80211PeerManager::preQueuePacket` drops every packet to it. `HtCapabilities::parse` reads both forms.
- `Node::unicast_mi` (`src/node.rs`) and `Own::umi_options` (`src/tlv.rs`): a MIF to one peer whose Data Path
  State carries flag `0x1000` and the options, as `IO80211AWDLPeer::parseAwdlDataPathTLVAndTakeAction` reads them
  and `processUnicastAWSF` acts on them. Other frames do not change.
- `Config::data_path_ext` and `Own::data_path_ext` (`src/node.rs`, `src/tlv.rs`): the extended flags that close
  the Data Path State, 0 by default. `parseAwdlDataPathTLVAndTakeAction` stores bit `0x800` of them as the
  peer's 6E capability, and `IO80211AWDLPeerManager::handleAirDropTrafficReg` sends a UMI only to a peer with it.
- `Config::service_params`, `Config::services` and the matching `Own` fields (`src/node.rs`, `src/tlv.rs`): the
  Service Parameters body (update indicator and bitmap) for every frame and whole Service Response TLVs (2) appended
  to each MIF, empty by default. An iPhone announces its AirDrop receiver this way, not by multicast mDNS.
- `Node::set_services` (`src/node.rs`): replaces both while the node runs, for records that change, as an
  iPhone's Rapport `at=` and `sid=` do.
- `src/driver.rs` removed with its `driver` feature (tokio), unused here.
- `Cargo.toml` written standalone (the workspace one is not vendored).
