# Huecos de pruebas en rutas de recepción inalámbrica

## Alcance y método

Auditoría estática de `Tools/*_test.c`, scripts de test correspondientes, `Source/Driver/*.c` y los puntos de entrada del árbol `Source/rtl8814au/core/` que los invocan. “Cubierto” significa que un test llama o compila explícitamente la unidad/rutina; no implica cobertura de todas sus ramas. No se ejecutaron tests ni se modificó código.

## Matriz de `Source/Driver/*.c`

| Unidad | Evidencia de cobertura existente | Ruta o área sin prueba de integración desde RX/aire |
|---|---|---|
| `CSA.c` | `Tools/csa_test.c` ejercita `rtl_csa_parse` y `rtl_csa_pick_bw` con CSA, Extended CSA, IE truncado y ancho de banda. | No se prueba el flujo de beacon/probe response hasta `process_csa_ie`, selección de canal disponible, deduplicación/espera y `rtw_set_chbw_cmd` (`Source/rtl8814au/core/rtw_wlan_util.c:2717-2760`). |
| `PMF.c` | `Tools/pmf_test.c` verifica validadores, cifrado/descifrado CCMP y BIP con vectores. También se compila junto al harness WPA2 de `Tools/run_eapol_flow_test.sh`. | No hay test del despacho de management RX completo: decrypt/trim, protección, SA Query y entrega o descarte a través de `validate_recv_mgnt_frame` (`Source/rtl8814au/core/rtw_recv.c:1969-2004`). |
| `ReplayCheck.c` | `Tools/replay_test.c` cubre orden, duplicados, contadores antiguos, reinicio de exchange y carry de 64 bits; `Tools/eapol_flow_test.c` lo ejercita integrado con EAPOL. | No se simulan los callbacks/transporte USB ni concurrencia de recepción real; el test integrado inyecta frames directamente en el supplicant. |
| `SAE.c` | `Tools/sae_test.c` comprueba vectores SAE P-256, commit/confirm, tokens, limpieza y KDF; el harness EAPOL también compila/usa funciones de esta unidad. | No se verifica que el flujo completo de autenticación/association en la radio y el AP intercambie correctamente estos frames. |
| `SAEAuth.c` | No aparece llamado ni compilado desde los tests encontrados. | Construcción y envío de Authentication frames SAE (`rtl_sae_issue_auth`, `SAEAuth.c:17-74`), incluidos allocator, atributos TX, secuencia, status y envío por la cola management, carecen de prueba host/unidad. |
| `SAEJoin.c` | No aparece llamado ni compilado por `Tools/*_test.c`. El harness de SAE prueba la criptografía subyacente, no esta máquina de estados. | `rtl_sae_join_prepare/start/rx_auth/timeout/cancel`, desafío de token, retries, worker/workqueue, invalidación por cambio de asociación y generación de RSN (`SAEJoin.c:322-679`) carecen de harness determinista o replay de traza RX/TX. El RX real entra desde `rtw_mlme_ext.c:2389-2390`. |
| `SecurityIE.c` | `Tools/security_ie_test.c` verifica RSN/KDE válidos y malformados; `Tools/eapol_flow_test.c` lo usa al procesar key data descifrado. | Falta una prueba de captura de frame completo que una parser, WPA2 supplicant y recepción USB; lo existente entrega directamente key-data/frame al nivel de harness. |
| `VendorProbe.c` | No hay test automatizado de esta unidad; `Tools/test_uc.c` es un cliente de diagnóstico/control de userspace, no un harness de la unidad. | Sin test del lifecycle USB probe/remove/suspend/resume, callback/encolado TX, llamadas de scan/join y validaciones de ABI/radio (`VendorProbe.c:403-1539`). |
| `WPA2Supplicant.c` | `Tools/eapol_flow_test.c` incluye la unidad de producción directamente y `Tools/run_eapol_flow_test.sh` la compila con `SAE.c`, `SecurityIE.c`, `PMF.c` y `ReplayCheck.c`. Inyecta EAPOL M1/M3/group, retransmisiones, replay, MIC, KDE malformado y truncamiento. | Buen test del state machine de supplicant aislado con stubs, no del recorrido de frames desde USB/`rtw_recv_entry`, de la cola real de transmisión, ni de integración hardware/driver. `rtl8814_wpa2_pmf_rx/tx` tampoco está probado a través del dispatcher real de management frames. |

## Lectura transversal

- Hay cobertura directa útil para los parsers/criptografía (`CSA`, `PMF`, `ReplayCheck`, `SAE`, `SecurityIE`) y una simulación de protocolo EAPOL centrada en `WPA2Supplicant`.
- No se encontró un test automatizado que reproduzca la ruta de extremo a extremo desde un frame recibido por USB, pasando por demultiplexado/validación/reorder, hasta los entrypoints de `SAEJoin`, `WPA2Supplicant` o el cambio de canal. La entrada de management RX que llama PMF está en `Source/rtl8814au/core/rtw_recv.c:1969-2004`; la llamada CSA está en `Source/rtl8814au/core/rtw_wlan_util.c:2717-2760`.
- `Tools/test_all.c` y `Tools/test_compat.sh` son pruebas de compilación/compatibilidad de shims; no cubren lógica radio. `Tools/test_uc.c` es una herramienta interactiva sobre el UserClient, no una prueba aislada del flujo RX.
- Esta lista es por unidad y ruta explícitamente conectadas desde los tests inspeccionados; no afirma ausencia de pruebas manuales/hardware ni cobertura interna de cada rama de una unidad marcada como cubierta.
