# Inventory of AWDL log strings in wifip2pd

Inventory without interpretation, grouped by topic. Origin: string extraction from `wifip2pd.x86_64` itself (md5 `a120cfccfa2783303604ce5be9e1763a`) over `__TEXT` (VA = `0x100000000` + file offset). For a sample of strings, the function using them was resolved with the rea/IDA adapter's xrefs; the rest stays `?`. Terms searched: `AWDL/awdl`, `Election`, `Sync`, `Master`, `Channel Sequence`, `Presence`, `OOB`, `Peer`.

## 1) Subsystems and Swift types (module identifiers)

| VA | String |
|---|---|
| `0x10044c8c0` | `wifip2pd.awdl-main` |
| `0x10044c8d3` | `wifip2pd.awdl` |
| `0x10044df2c` | `com.apple.awdl` |
| `0x10044e37a` | `awdl` |
| `0x10044e660` | `com.apple.wifi.AwdlRtgStatistics` |
| `0x10044ebd0` | `com.apple.wifip2pd.awdl_bonjour` |
| `0x10044ffc0` | `com.apple.wifip2pd.awdl_transaction` |
| `0x10044fff0` | `wifip2pdCore/AWDLInterface.swift` |
| `0x1004501a0` | `wifip2pdCore.AWDLInterface` |
| `0x10044f420` | `wifip2pdCore.AWDLNetworkAgent` |
| `0x10044eec0` | `AWDLDatapathInitiator ` |
| `0x10044f400` | `AWDL Network Agent` |
| `0x1004575f2` | `$s7CoreP2P14AWDLWiFiDriverP` (Swift protocol) |
| `0x10045760e` | `$s7CoreP2P14AWDLControllerP` (Swift protocol) |
| `0x10045b310` | `AWDLTrafficRegistrationService` |

## 2) Election

| VA | String | Function |
|---|---|---|
| `0x1004475d0` | `update(electionMetric:)` | `?` |
| `0x100447650` | `update(hostElection:)` | `?` |
| `0x1004476d0` | `update(electionParameters:)` | `?` |
| `0x100447b10` | `Continuity Real Time Group RTG Sync Election, ` | `?` |
| `0x100447b40` | `Remote Screen RTG Sync Election, ` | `?` |
| `0x100448380` | `selfElectionMetric` | `?` |
| `0x10044deca` | `election` | `?` |
| `0x10044fa70` | ` election metric` | `?` |
| `0x100450430` | `STATION_AWDL_ELECTION_METRIC` (metric key) | `?` |
| `0x10046a1a0` | `DeprecatedElectionParameter` | `?` |

## 3) Sync / Master / Channel Sequence

| VA | String | Function |
|---|---|---|
| `0x100448550` | `Non Master Non Sync` | `?` |
| `0x100448270` | `masterIndication` | `?` |
| `0x100448290` | `anchorMasterAddress` | `?` |
| `0x1004482b0` | `anchorMasterRandomFactor` | `?` |
| `0x1004482d0` | `anchorMasterPreference` | `?` |
| `0x1004482f0` | `anchorMasterHopCount` | `?` |
| `0x100448310` | `anchorMasterBeaconTransmissionTime` | `?` |
| `0x1004483a0` | `masterHomeChannel` | `?` |
| `0x100448570` | `masterPreference` | `?` |
| `0x10044fc00` | `immediateMasterRankM` | `?` |
| `0x10044fc20` | `immediateMasterRankR` | `?` |
| `0x10044d980` | `anchorMasterRank` | `?` |
| `0x100448510` | `channelSequenceParameter` | `?` |
| `0x100448b60` | `updatedChannelSequence` | `?` |
| `0x10044c190` | `has2GInChannelSequence` | `?` |
| `0x10044c290` | `channelSequenceMismatchCount` | `?` |
| `0x10044d400` | `channelSeqMismatchCount5G` | `?` |
| `0x10044d420` | `channelSeqMismatchCount2G` | `?` |
| `0x100450250` | `AWDL_2pt4GHZ_IN_CHAN_SEQ` | `?` |
| `0x1004773a8` | `ChannelSequence` | `?` |
| `0x10046a120` | `AWDLChannelSequenceParameter` | `?` |
| `0x10046a560` | `ExtendedChannelSequence` | `?` |
| `0x1004a40f0` | `Peer %s not synced.` | `sub_1003B3F10` (xref `0x1003b713a`) |
| `0x1004a9d2e` | `channelSequenceChangedEvent:` | `?` |
| `0x100450470` | `STATION_AWDL_TOP_MASTER` (metric key) | `?` |
| `0x100450490` | `STATION_AWDL_AWDL_MASTER` (metric key) | `?` |

## 4) Presence (peer presence)

| VA | String | Function |
|---|---|---|
| `0x100495b30` | `Post peer presence for %s` | `sub_1000CA0F0` (xref `0x1000ca2fa`), `sub_1000D6000` (xref `0x1000d615c`) |
| `0x10049a020` | `Posted peer presence for %s on %s` | `?` |
| `0x10049a050` | `Posted peer absence for %s on %s` | `?` |
| `0x10049a2f0` | `Failed to post peer absence for %s because %@` | `?` |
| `0x10049a960` | `Failed to post peer presence for %s because %@` | `?` |
| `0x1004a3890` | `Failed to initially clear the peer presence table because %@` | `?` |
| `0x1004a6b30` | `Will clear traffic registration for %s to %s because failed to post peer presence on %s: %@` | `?` |
| `0x1004a7ae0` | `Failed to clear all previous peer presence after interface recovery` | `?` |
| `0x1004a7f90` | `Failed to set the peer presence for %s: %@` | `?` |
| `0x10045cfe0` | `apple80211_peer_presence_indication` (struct name) | `?` |
| `0x10045e462` | `peerPresence` | `?` |
| `0x1004db080` | `peerPresencePosted` | `?` |

## 5) OOB (out-of-band action frames)

| VA | String | Function |
|---|---|---|
| `0x1004a2110` | `Attempting to transmit OOB AF [mapId=%s] to peer: %s (5G=%{bool}d, 2G=%{bool}d), supportsSimultaneousDualBand=%{bool}d dynamicSDB=%{bool}don clusterID: %s` | `?` |
| `0x100447aa0` | `Failed to convert raw apple80211_act_frm_hdr to AWDLActionFrame.Header.` | `?` |
| `0x100447bc0` | `AWDL To WiFi-Aware Migration, ` | `?` |

## 6) Peer / traffic / datapath (narrative selection)

| VA | String | Function |
|---|---|---|
| `0x1004933b0` | `Cannot resolve %s because peer not discovered with %s` | `?` |
| `0x100493510` | `Peer not discovered yet for %s, queueing resolve request` | `?` |
| `0x100493db0` | `Failed to update IP address in driver for new infra peer %s due to %@` | `?` |
| `0x100493e00` | `Infra peer (%s) has IP address %s` | `?` |
| `0x100495cd0` | `Paired Peer: Installing peer NIK` | `?` |
| `0x100498dc0` | `Installing peer with UUID: %s` | `?` |
| `0x10049a910` | `Peer: %s [NMI: %s]` | `?` |
| `0x10049b570` | `%@ failed to clear traffic registration for %s to %s because invalid peer address` | `?` |
| `0x100493940` | `Failed to find the interface name returned when creating the low latency interface for %s` | `?` |
| `0x1004939a0` | `Timed out while waiting for the low latency interface with parent %s` | `?` |
| `0x100450290` | `Wi-Fi AWDL frequently active with no data because of ` | `?` |
| `0x100450300` | `: AWDL kept enabled for more than ` | `?` |
| `0x100450350` | `AWDL kept enabled for more than ` | `?` |
| `0x100450270` | `AWDL_REALTIME_TRAFFIC_START` (metric key) | `?` |

## 7) Metric keys (`AWDL_STATS_*` / `APPLE80211KEY_*`)

338 strings matching `awdl` in `__TEXT`; the vast majority are statistics dictionary keys, in two contiguous blocks:
- Block `AWDL_STATS_*` / `APPLE80211KEY_AWDL_STATS_*`: `0x1004499d0`–`0x10044a2c0` (~90 keys; examples: `AWDL_STATS_NUM_PEERS` `0x100449a90`, `AWDL_STATS_NUM_SYNC_CHANGES` `0x100449c00`, `AWDL_STATS_PEER_VERSION_BITMAP` `0x100449cc0`, `AWDL_STATS_NUM_CACHED_PEERS_6GHZ` `0x100449f10`).
- Block `APPLE80211KEY_AWDL_AIRPLAY_*` / `AWDL_RTG_STATS_*`: `0x10044a290`–`0x10044ae20` (~70 keys; examples: `AWDL_RTG_STATS_SELF_INFRA_CHANNEL` `0x100449…`/`0x100449980`, `AWDL_RTG_STATS_FORCEROAM_OUTOF6G_COUNT` `0x10044ab60`, `AWDL_STATS_HISTOGRAM_BIN_START` `0x10044b9e0`).
- Others: `STATION_AWDL_*` (`0x10044a300`, `0x100450430`–`0x1004504f0`), `LL_STATS_REDUCED_AWDL_BW` `0x10044b3c0`, `AWDL_STATS_SERVICE_*` `0x10044ba40`-`0x10044ba60`.

Function using them: `?` (consumption per key, typical of statistics dictionaries; not resolved by xref in this pass).

## Method and limits

- Full deterministic extraction from the file (`strings -t x` over `__TEXT`, 10,297 strings, VA = base + offset; the binary is not stripped of its constants).
- "Function" resolved only for 3 strings via the adapter's `xrefs` (sections 4 and 3); strings in `__cstring` are referenced with `lea` and the xrefs appear as `data` pointing inside the using function.
- Searches with the adapter (`search_strings`) were not used because that provider's catalog does not filter (it returns the full inventory); the file extraction is equivalent and verifiable.
