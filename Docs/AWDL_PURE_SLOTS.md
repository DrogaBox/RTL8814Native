# Slots puros de IO80211AWDLProtocol resueltos con la vtable de Broadcom

Complementa `AWDL_VTABLE_SLOTS.md`, que dejó la columna Proximity en `?` por falta de datos. Aquí se completa con la vtable de `AppleBCMWLANProximityInterface` (`__ZTV` en `0xffffff80017f5b68`, tipo `0e`) leída directamente del IDB de `BootKernelExtensions.kc` (macOS 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`), celdas `__ZTV + índice*8`, desde el slot 490 hasta el 572. Los nombres salen de `Docs/re/io80211_25.6.0/syms_bcm.txt` por coincidencia exacta de la dirección destino; ninguno se dedujo por orden.

Lectura: Broadcom sobrescribe cada slot puro (490 y 507–569) con un método concreto, así que una subclase propia debe implementar estas 64 firmas. Los slots 570–572 (`getTX_RATE`, `init(core, mac, u32, name)`, `freeResources`) son propios de Broadcom y no son puros en la base.

| Slot | Destino | Método Broadcom (firma desmangleada) |
|---:|---|---|
| 490 | `0xffffff8001720354` | `setMacAddress(ether_addr&)` |
| 507 | `0xffffff800171a70e` | `getSSID(apple80211_ssid_data*)` |
| 508 | `0xffffff800171a752` | `getCHANNEL(apple80211_channel_data*)` |
| 509 | `0xffffff800171a702` | `getBSSID(apple80211_bssid_data*)` |
| 510 | `0xffffff8001719944` | `getSTATE(apple80211_state_data*)` |
| 511 | `0xffffff800171b1b8` | `getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params*)` |
| 512 | `0xffffff800171b092` | `getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled*)` |
| 513 | `0xffffff800171be78` | `getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter*)` |
| 514 | `0xffffff800171c456` | `getAWDL_ELECTION_ID(apple80211_awdl_election_id*)` |
| 515 | `0xffffff800171cd70` | `getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric*)` |
| 516 | `0xffffff800172421a` | `getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence*)` |
| 517 | `0xffffff80017233b6` | `getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size*)` |
| 518 | `0xffffff800171f1e6` | `getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel*)` |
| 519 | `0xffffff800171b528` | `getAWDL_SYNC_STATE(apple80211_awdl_sync_state*)` |
| 520 | `0xffffff800171d2e4` | `getAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds*)` |
| 521 | `0xffffff800171bd12` | `getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode*)` |
| 522 | `0xffffff800171ce5a` | `getAWDL_ELECTION_PARAMS(apple80211_awdl_election_params*)` |
| 523 | `0xffffff800171be38` | `getAWDL_DEVICE_CAPABILITIES(apple80211_awdl_device_capabilities*)` |
| 524 | `0xffffff800171ec10` | `getAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel*)` |
| 525 | `0xffffff8001719b62` | `getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode*)` |
| 526 | `0xffffff8001728a3c` | `getAWDL_MIN_RATE(apple80211_awdl_min_rate*)` |
| 527 | `0xffffff800171f900` | `getAWDL_ENCRYPTION_TYPE(apple80211_encrypt*)` |
| 528 | `0xffffff8001728cae` | `getAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*)` |
| 529 | `0xffffff800171b698` | `getAWDL_DEV_STATS(apple80211_awdl_dev_stats*)` |
| 530 | `0xffffff8001728fba` | `getAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*)` |
| 531 | `0xffffff800171ba8e` | `getAWDL_CCA(apple80211_awdl_cca*)` |
| 532 | `0xffffff800171ed38` | `getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(apple80211_awdl_cap*)` |
| 533 | `0xffffff800172753e` | `getLLW_PARAMS(apple80211_llw_params*)` |
| 534 | `0xffffff8001715a70` | `setSSID(apple80211_ssid_data*)` |
| 535 | `0xffffff800171e8d8` | `setCHANNEL(apple80211_channel_data*)` |
| 536 | `0xffffff800171ddee` | `setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params*)` |
| 537 | `0xffffff800171e42a` | `setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled*)` |
| 538 | `0xffffff800171bfdc` | `setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter*)` |
| 539 | `0xffffff800171cace` | `setAWDL_ELECTION_ID(apple80211_awdl_election_id*)` |
| 540 | `0xffffff800172181a` | `setAWDL_BSSID(apple80211_awdl_bssid*)` |
| 541 | `0xffffff800171cf76` | `setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric*)` |
| 542 | `0xffffff8001724436` | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence*)` |
| 543 | `0xffffff800171ea1a` | `setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel*)` |
| 544 | `0xffffff800171e164` | `setAWDL_SYNC_STATE(apple80211_awdl_sync_state*)` |
| 545 | `0xffffff800171d16a` | `setAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds*)` |
| 546 | `0xffffff80017231d8` | `setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode*)` |
| 547 | `0xffffff80017235ce` | `setPEER_CACHE_CONTROL(apple80211_peer_cache_control*)` |
| 548 | `0xffffff8001726474` | `setAWDL_OOB_REQUEST(apple80211_awdl_oob_request*)` |
| 549 | `0xffffff800171da80` | `setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template*)` |
| 550 | `0xffffff800171c4ca` | `setAWDL_ELECTION_PARAMS(apple80211_awdl_election_params*)` |
| 551 | `0xffffff800171fcaa` | `setAWDL_PEER_TRAFFIC_REGISTRATION(apple80211_awdl_peer_traffic_registration*)` |
| 552 | `0xffffff800171f01e` | `setAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel*)` |
| 553 | `0xffffff80017199c4` | `setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode*)` |
| 554 | `0xffffff8001728a48` | `setAWDL_MIN_RATE(apple80211_awdl_min_rate*)` |
| 555 | `0xffffff800171f480` | `setAWDL_ENCRYPTION_KEYS(apple80211_key*)` |
| 556 | `0xffffff800171fa46` | `setAWDL_ENCRYPTION_TYPE(apple80211_encrypt*)` |
| 557 | `0xffffff8001727c0e` | `setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request*)` |
| 558 | `0xffffff8001728b8c` | `setAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*)` |
| 559 | `0xffffff800171c932` | `setAWDL_AWDL_ADVERTISERS(awdl_peer_advert_add*)` |
| 560 | `0xffffff8001728dd0` | `setAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*)` |
| 561 | `0xffffff800172919c` | `setAWDL_DFSP_UCSA_CONFIG(apple80211_awdl_dfsp_ucsa_config*)` |
| 562 | `0xffffff8001722ef2` | `setCOMPANION_SKYWALK_IF_CREATE(apple80211_companion_skywalk_if_create*)` |
| 563 | `0xffffff8001727d56` | `setAWDL_OOB_SEC_PAYLOAD_REQUEST(apple80211_awdl_af_sec_payload_request*)` |
| 564 | `0xffffff8001726ac8` | `setLLW_PARAMS(apple80211_llw_params*)` |
| 565 | `0xffffff8001720c20` | `setTX_RATE(apple80211_tx_rate*)` |
| 566 | `0xffffff800172a968` | `setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection*)` |
| 567 | `0xffffff800171dfc0` | `setPSF_DWELL(apple80211_awdl_psf_dwell_params*)` |
| 568 | `0xffffff80017215e2` | `setAWDL_CONFIG(apple80211_awdl_config_request*)` |
| 569 | `0xffffff800171fb6c` | `setAWDL_RTG_PEER_STATS_CONFIG(apple80211_rtg_peer_stats_cfg*)` |
| 570 | `0xffffff8001720ade` | `getTX_RATE(apple80211_tx_rate*)` |
| 571 | `0xffffff8001718db0` | `init(AppleBCMWLANCore*, ether_addr*, unsigned int, char*)` |
| 572 | `0xffffff80017194c8` | `freeResources()` |

Observaciones:
- El slot 490 es `setMacAddress(ether_addr&)`, único puro propio de `IO80211VirtualInterface`.
- Los slots 507–569 son los getters/setters Apple80211 de AWDL (`get/setAWDL_*`, `getSSID/CHANNEL/BSSID/STATE`, `setLLW_PARAMS`, `setPEER_CACHE_CONTROL`, `setCOMPANION_SKYWALK_IF_CREATE`, etc.). Cada uno recibe un puntero a una estructura `apple80211_*` cuyo layout aún hay que declarar.
- Verificado contra el IDB abierto; no se ejecutó nada en hardware.
