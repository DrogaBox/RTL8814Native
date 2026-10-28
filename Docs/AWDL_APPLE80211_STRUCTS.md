# Estructuras Apple80211 de las firmas AWDL

## Alcance y criterio

Inventario construido sólo con evidencia guardada: firmas de la vtable Broadcom resueltas en [AWDL_PURE_SLOTS.md](AWDL_PURE_SLOTS.md), símbolos del KC en [syms_bcm.txt](re/io80211_25.6.0/syms_bcm.txt), type dump conservado de `wifip2pd` en [awdl_callers_probe.log](../Build/ida_awdl/awdl_callers_probe.log) y headers locales de `Source/IO80211/`. No consulté IDA/MCP ni generé nuevos volcados.

`Tamaño = ?` significa que estas fuentes no dan el `sizeof` de la estructura. Los nombres de tipos de parámetro y los símbolos demuestran las firmas, pero no su layout. Los campos se anotan sólo cuando aparecen explícitamente en el type dump. No infiero campos, offsets ni padding a partir de nombres, mangling o tamaños de otros requests.

## Tipos de parámetro en las firmas AWDL

Cada fila es un tipo distinto extraído de los parámetros de la vtable Broadcom: slots 507–569 y el slot heredado 490. Las firmas son evidencia del tipo de parámetro, no de su tamaño en memoria.

| Tipo | Tamaño | Campos demostrados | Firma Broadcom / slot | Fuente exacta |
|---|---:|---|---|---|
| `apple80211_ssid_data` | `?` | `?` | `getSSID` / 507; `setSSID` / 534 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1308,1338`; firma verificada: `Docs/AWDL_PURE_SLOTS.md:10,37` |
| `apple80211_channel_data` | `?` | `?` | `getCHANNEL` / 508; `setCHANNEL` / 535 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1339,1375`; `Docs/AWDL_PURE_SLOTS.md:11,38` |
| `apple80211_bssid_data` | `?` | `?` | `getBSSID` / 509 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1337`; `Docs/AWDL_PURE_SLOTS.md:12` |
| `apple80211_state_data` | `?` | `?` | `getSTATE` / 510 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1330`; `Docs/AWDL_PURE_SLOTS.md:13` |
| `apple80211_awdl_sync_params` | `?` | `?` | `getAWDL_SYNC_PARAMS` / 511; `setAWDL_SYNC_PARAMS` / 536 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1344,1370`; `Docs/AWDL_PURE_SLOTS.md:14,39` |
| `apple80211_awdl_sync_enabled` | `?` | `version: UInt32`, `flags: UInt32`, `enabled: UInt32` (type dump; no ABI `sizeof` inferred) | `getAWDL_SYNC_ENABLED` / 512; `setAWDL_SYNC_ENABLED` / 537 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1343,1373-1374`; `Docs/AWDL_PURE_SLOTS.md:15,40`; campos: `Build/ida_awdl/awdl_callers_probe.log:1345-1348` |
| `apple80211_awdl_extension_state_machine_parameter` | `?` | `?` | get / 513; set / 538 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1350-1351`; `Docs/AWDL_PURE_SLOTS.md:16,41` |
| `apple80211_awdl_election_id` | `?` | `?` | get / 514; set / 539 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1354,1358`; `Docs/AWDL_PURE_SLOTS.md:17,42` |
| `apple80211_awdl_election_metric` | `?` | `?` | get / 515; set / 541 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1359,1361`; `Docs/AWDL_PURE_SLOTS.md:18,44` |
| `apple80211_awdl_sync_channel_sequence` | `?` | `?` | get / 516; set / 542 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1402-1405`; `Docs/AWDL_PURE_SLOTS.md:19,45` |
| `apple80211_peer_cache_maximum_size` | `?` | `?` | `getPEER_CACHE_MAXIMUM_SIZE` / 517 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1400`; `Docs/AWDL_PURE_SLOTS.md:20` |
| `apple80211_awdl_master_channel` | `?` | `?` | get / 518; set / 543 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1376,1380`; `Docs/AWDL_PURE_SLOTS.md:21,46` |
| `apple80211_awdl_sync_state` | `?` | `?` | get / 519; set / 544 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1345,1372`; `Docs/AWDL_PURE_SLOTS.md:22,47` |
| `apple80211_awdl_election_rssi_thresholds` | `?` | `?` | get / 520; set / 545 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1362-1363`; `Docs/AWDL_PURE_SLOTS.md:23,48` |
| `apple80211_awdl_presence_mode` | `?` | `?` | get / 521; set / 546 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1348,1399`; `Docs/AWDL_PURE_SLOTS.md:24,49` |
| `apple80211_awdl_election_params` | `?` | `?` | get / 522; set / 550 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1355,1360`; `Docs/AWDL_PURE_SLOTS.md:25,53` |
| `apple80211_awdl_device_capabilities` | `?` | `?` | `getAWDL_DEVICE_CAPABILITIES` / 523 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1349`; `Docs/AWDL_PURE_SLOTS.md:26` |
| `apple80211_awdl_secondary_master_channel` | `?` | `?` | get / 524; set / 552 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1377,1379`; `Docs/AWDL_PURE_SLOTS.md:27,55` |
| `apple80211_awdl_af_tx_mode` | `?` | `?` | get / 525; set / 553 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1331-1332`; `Docs/AWDL_PURE_SLOTS.md:28,56` |
| `apple80211_awdl_min_rate` | `?` | `?` | get / 526; set / 554 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1415-1416`; `Docs/AWDL_PURE_SLOTS.md:29,57` |
| `apple80211_encrypt` | `?` | `?` | `getAWDL_ENCRYPTION_TYPE` / 527; `setAWDL_ENCRYPTION_TYPE` / 556 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1382-1383`; `Docs/AWDL_PURE_SLOTS.md:30,59` |
| `apple80211_awdl_txcal_period` | `?` | `?` | get / 528; set / 558 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1417-1418`; `Docs/AWDL_PURE_SLOTS.md:31,61` |
| `apple80211_awdl_dev_stats` | `?` | `?` | `getAWDL_DEV_STATS` / 529 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1346`; `Docs/AWDL_PURE_SLOTS.md:32` |
| `apple80211_awdl_dfsp_config` | `?` | `?` | get / 530; set / 560 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1419-1420`; `Docs/AWDL_PURE_SLOTS.md:33,63` |
| `apple80211_awdl_cca` | `?` | `?` | `getAWDL_CCA` / 531 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1347`; `Docs/AWDL_PURE_SLOTS.md:34` |
| `apple80211_awdl_cap` | `?` | `?` | `getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES` / 532 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1378`; `Docs/AWDL_PURE_SLOTS.md:35` |
| `apple80211_llw_params` | `?` | `?` | get / 533; set / 564 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1408-1409`; `Docs/AWDL_PURE_SLOTS.md:36,67` |
| `apple80211_awdl_bssid` | `?` | `?` | `setAWDL_BSSID` / 540 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1395`; `Docs/AWDL_PURE_SLOTS.md:43` |
| `apple80211_peer_cache_control` | `?` | `?` | `setPEER_CACHE_CONTROL` / 547 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1401`; `Docs/AWDL_PURE_SLOTS.md:50` |
| `apple80211_awdl_oob_request` | `?` | `?` | `setAWDL_OOB_REQUEST` / 548; auto request / 557 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1406,1410`; `Docs/AWDL_PURE_SLOTS.md:51,60` |
| `apple80211_awdl_sync_frame_template` | `?` | `?` | `setAWDL_SYNC_FRAME_TEMPLATE` / 549 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1368`; `Docs/AWDL_PURE_SLOTS.md:52` |
| `apple80211_awdl_peer_traffic_registration` | `?` | `?` | `setAWDL_PEER_TRAFFIC_REGISTRATION` / 551 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1385`; `Docs/AWDL_PURE_SLOTS.md:54` |
| `apple80211_key` | `?` | `?` | `setAWDL_ENCRYPTION_KEYS` / 555 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1381`; `Docs/AWDL_PURE_SLOTS.md:58` |
| `awdl_peer_advert_add` | `?` | ver campos enumerados abajo; el dump no da tamaño ni offsets | `setAWDL_AWDL_ADVERTISERS` / 559 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1357`; `Docs/AWDL_PURE_SLOTS.md:62`; campos: `Build/ida_awdl/awdl_callers_probe.log:1298-1317` |
| `apple80211_awdl_dfsp_ucsa_config` | `?` | `?` | `setAWDL_DFSP_UCSA_CONFIG` / 561 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1421`; `Docs/AWDL_PURE_SLOTS.md:64` |
| `apple80211_companion_skywalk_if_create` | `?` | `?` | `setCOMPANION_SKYWALK_IF_CREATE` / 562 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1397`; `Docs/AWDL_PURE_SLOTS.md:65` |
| `apple80211_awdl_af_sec_payload_request` | `?` | `?` | `setAWDL_OOB_SEC_PAYLOAD_REQUEST` / 563 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1411`; `Docs/AWDL_PURE_SLOTS.md:66` |
| `apple80211_tx_rate` | `?` | `?` | `setTX_RATE` / 565; `getTX_RATE` / 570 (extra Broadcom slot) | `Docs/re/io80211_25.6.0/syms_bcm.txt:1390-1391`; `Docs/AWDL_PURE_SLOTS.md:68,74` |
| `apple80211_p2p_coex_protection` | `?` | `?` | `setP2P_COEX_PROTECTION` / 566 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1442`; `Docs/AWDL_PURE_SLOTS.md:69` |
| `apple80211_awdl_psf_dwell_params` | `?` | `?` | `setPSF_DWELL` / 567 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1371`; `Docs/AWDL_PURE_SLOTS.md:70` |
| `apple80211_awdl_config_request` | `?` | `?` | `setAWDL_CONFIG` / 568 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1394`; `Docs/AWDL_PURE_SLOTS.md:71` |
| `apple80211_rtg_peer_stats_cfg` | `?` | `?` | `setAWDL_RTG_PEER_STATS_CONFIG` / 569 | `Docs/re/io80211_25.6.0/syms_bcm.txt:1384`; `Docs/AWDL_PURE_SLOTS.md:72` |

## Estructuras de creación y tipos auxiliares

| Tipo | Tamaño | Campos demostrados | Fuente exacta |
|---|---:|---|---|
| `apple80211_virt_if_create_data` | `32 bytes` | `role`: dword en `+12`; nombre BSD de salida en `+16` (hasta 16 bytes); otros campos `?` | `Source/IO80211/IO80211_25_6_0.h:206,685` sólo declara el nombre y la firma; análisis del handler citado en `Docs/AWDL_ROLE6_RE.md:65,96`; función Broadcom en `Build/ida_awdl/core.log:49` (VA `0xffffff80016788f4`). |
| `apple80211_companion_skywalk_if_create_code` | `?` | `rawValue: UInt32` (tipo auxiliar; no demuestra el layout de `apple80211_companion_skywalk_if_create`) | `Build/ida_awdl/awdl_callers_probe.log:1666-1667`. |

El type dump de `wifip2pd` también enumera `apple80211_awdl_strategy`, `apple80211_awdl_social_time_slots`, `apple80211_awdl_oper_mode`, `apple80211_awdl_UMI_data`, `apple80211_awdl_preferred_channels`, `apple80211_awdl_info_ext`, `apple80211_awdl_ppp_done`, `apple80211_awdl_d2d_migration_statistics` y `apple80211_awdl_state_statistics`. Para ellos constan miembros en el dump, pero no hay base para declararlos layouts de los parámetros de la tabla salvo los que coinciden literalmente por nombre; sus `sizeof` también siguen como `?`. El detalle de esos miembros está en `Build/ida_awdl/awdl_callers_probe.log:1188-1190,1298-1317,1325-1348,1699-1712`.

En `awdl_peer_advert_add`, el dump enumera `aw_counter`, `tx_counter`, `tx_delay`, `period_tu`, `aw_cmn_length`, `aw_ext_length`, `aw_period` (`UInt16`); `rssi` (`Int8`); `guard_time`, `presence_mode`, `age`, `dist_top` (`UInt8`); `ms`, `self_metrics`, `top_master_metrics` (`UInt32`); `top_master` y `addr` (`ether_addr`). No proporciona tamaño, offsets ni padding.

## Resumen

Las firmas preservadas identifican **42 tipos distintos** y permiten mapear los nombres pedidos con funciones Broadcom. Sin embargo, salvo los miembros expresamente enumerados en el type dump de `wifip2pd`, los campos y tamaños son desconocidos; no hay layouts AWDL completos en los headers locales. `apple80211_virt_if_create_data` es la excepción parcial: su tamaño y los offsets usados están documentados por el handler; el header local sólo lo forward-declara.
