# Test gaps in wireless receive paths

## Scope and method

Static audit of `Tools/*_test.c`, the corresponding test scripts, `Source/Driver/*.c` and the entry points of the `Source/rtl8814au/core/` tree that call them. "Covered" means a test explicitly calls or compiles the unit/routine; it does not imply coverage of all its branches. No tests were run and no code was modified.

## Matrix of `Source/Driver/*.c`

| Unit | Existing coverage evidence | Path or area without an integration test from RX/air |
|---|---|---|
| `CSA.c` | `Tools/csa_test.c` exercises `rtl_csa_parse` and `rtl_csa_pick_bw` with CSA, Extended CSA, truncated IE and bandwidth. | The beacon/probe response flow down to `process_csa_ie`, available channel selection, deduplication/waiting and `rtw_set_chbw_cmd` is not tested (`Source/rtl8814au/core/rtw_wlan_util.c:2717-2760`). |
| `PMF.c` | `Tools/pmf_test.c` verifies validators, CCMP and BIP encryption/decryption with vectors. It is also compiled alongside the WPA2 harness of `Tools/run_eapol_flow_test.sh`. | There is no test of the full management RX dispatch: decrypt/trim, protection, SA Query and delivery or discard through `validate_recv_mgnt_frame` (`Source/rtl8814au/core/rtw_recv.c:1969-2004`). |
| `ReplayCheck.c` | `Tools/replay_test.c` covers ordering, duplicates, old counters, exchange reset and 64-bit carry; `Tools/eapol_flow_test.c` exercises it integrated with EAPOL. | The USB callbacks/transport and real receive concurrency are not simulated; the integrated test injects frames directly into the supplicant. |
| `SAE.c` | `Tools/sae_test.c` checks SAE P-256 vectors, commit/confirm, tokens, cleanup and KDF; the EAPOL harness also compiles/uses functions from this unit. | It is not verified that the full authentication/association flow between the radio and the AP exchanges these frames correctly. |
| `SAEAuth.c` | Not seen called or compiled from the tests found. | Construction and sending of SAE Authentication frames (`rtl_sae_issue_auth`, `SAEAuth.c:17-74`), including allocator, TX attributes, sequence, status and sending through the management queue, lack a host/unit test. |
| `SAEJoin.c` | Not called or compiled by any `Tools/*_test.c`. The SAE harness tests the underlying cryptography, not this state machine. | `rtl_sae_join_prepare/start/rx_auth/timeout/cancel`, token challenge, retries, worker/workqueue, invalidation on association change and RSN generation (`SAEJoin.c:322-679`) lack a deterministic harness or a RX/TX trace replay. Real RX enters from `rtw_mlme_ext.c:2389-2390`. |
| `SecurityIE.c` | `Tools/security_ie_test.c` verifies valid and malformed RSN/KDE; `Tools/eapol_flow_test.c` uses it when processing decrypted key data. | A full frame capture test that ties together the parser, the WPA2 supplicant and USB reception is missing; what exists hands key-data/frames directly to the harness level. |
| `VendorProbe.c` | There is no automated test of this unit; `Tools/test_uc.c` is a userspace diagnostic/control client, not a harness for the unit. | No test of the USB probe/remove/suspend/resume lifecycle, TX callback/queueing, scan/join calls and ABI/radio validations (`VendorProbe.c:403-1539`). |
| `WPA2Supplicant.c` | `Tools/eapol_flow_test.c` includes the production unit directly and `Tools/run_eapol_flow_test.sh` compiles it with `SAE.c`, `SecurityIE.c`, `PMF.c` and `ReplayCheck.c`. It injects EAPOL M1/M3/group, retransmissions, replay, MIC, malformed KDE and truncation. | Good test of the supplicant state machine isolated with stubs, not of the frame path from USB/`rtw_recv_entry`, of the real transmit queue, or of hardware/driver integration. `rtl8814_wpa2_pmf_rx/tx` is not tested through the real management frame dispatcher either. |

## Cross-cutting reading

- There is useful direct coverage for the parsers/cryptography (`CSA`, `PMF`, `ReplayCheck`, `SAE`, `SecurityIE`) and an EAPOL protocol simulation centred on `WPA2Supplicant`.
- No automated test was found that reproduces the end-to-end path from a frame received over USB, through demultiplexing/validation/reorder, down to the entrypoints of `SAEJoin`, `WPA2Supplicant` or the channel switch. The management RX input that calls PMF is at `Source/rtl8814au/core/rtw_recv.c:1969-2004`; the CSA call is at `Source/rtl8814au/core/rtw_wlan_util.c:2717-2760`.
- `Tools/test_all.c` and `Tools/test_compat.sh` are shim build/compatibility tests; they do not cover radio logic. `Tools/test_uc.c` is an interactive tool over the UserClient, not an isolated test of the RX flow.
- This list is per unit and path explicitly connected from the inspected tests; it does not assert the absence of manual/hardware tests or the internal coverage of every branch of a unit marked as covered.
