# WPA3-SAE: where it stands and what is left

Notes for whoever picks this up. Everything below was checked on my one setup (TP-Link Archer T9UH, macOS 26.7.1, OpenCore). Read `Docs/PHASE5_IO80211.md` (section "WPA3-SAE gates") first; this file is the to-do list that follows it.

## State

- Driver 4.22 and layer 0.30 are what is in my EFI right now. The layer is the 0.28 code plus three capability bits (`Source/AirLayer/RTLAirLayer.cpp`, `getCARD_CAPABILITIES`): `p[13] |= 0x08` (capability 75, SAE), `p[10] |= 0x10` (PMF), on top of the AWDL bits.
- With those bits macOS offers SAE on WPA3 networks, and `setWCL_ASSOCIATE` runs. For a WPA3-only hotspot it receives:
  - `key + 4` (key length) = the passphrase length, and the passphrase bytes at `key + 16`. For WPA2 it was always 32 (a PMK).
  - `req + 16` = 1 and `req + 20` = 0x1000 (open-system lower auth, SAE upper auth).
- In the deployed layer the join still fails because it only accepts a 32-byte PMK. The step 1 build below carries the passphrase but intentionally returns unsupported at the radio. `RTLAirLastAssoc` in ioreg keeps the header of the last request (no key bytes).
- Side effect: macOS now also tries SAE on my WPA2/WPA3 mixed router, and that join fails. Layer 0.28 (`Backups/airlayer_0.28`, md5 `7066099f...`) does not have the bits and keeps the router working. To go back, point `LAYER_SRC`/`LAYER_MD5` in `Tools/phase5_deploy.sh` at it and deploy.

## What the vendor code gives us

- `Source/rtl8814au/core/rtw_ieee80211.c` knows the SAE AKM selectors (00-0F-AC:8 and :9) and nothing more. There is no SAE logic in the vendor tree.
- `OnAuthClient` and `issue_auth` in `core/rtw_mlme_ext.c` are the places to hook the auth exchange. `OnAuthClient` has a cfg80211 branch for SAE that is not compiled here (we have no cfg80211). `issue_auth` always builds an open-system or shared-key frame, so SAE needs its own sender with algorithm 3 and an arbitrary body.
- `core/rtw_security.c` has BIP/IGTK code behind `CONFIG_IEEE80211W`. That flag is not set in `build.sh`. Turning it on has not been tried and may pull in files we do not build.
- `Source/Driver/WPA2Supplicant.c` is the in-kernel supplicant: self-contained SHA-1/HMAC-SHA1/PBKDF2, PRF-384, AES key unwrap, the 4-way handshake and the key install into the CAM. It only does AKM 2 (PSK) with key descriptor version 2.

## What has to be written

1. **Plumbing.** `struct rtl_radio_join` (`Source/Driver/RTLRadioABI.h`) has no way to carry a passphrase. Add an SAE flag and a passphrase field at the end and bump `RTL_RADIO_ABI`. In the layer (`setWCL_ASSOCIATE`) accept `keyLen != 32` when the upper auth is SAE and copy the passphrase through. In the driver, `rtl8814native_radio_join` (`Source/Driver/VendorProbe.c`) and `rtl8814_wpa2_configure` need an SAE branch. Wipe the passphrase after use, as the PMK is wiped today.
2. **SAE exchange** (IEEE 802.11-2020, section 12.4): the standalone group-19 hunt-and-peck exchange is written and vector-tested below. It is not connected to the radio yet. Before using it, provide secure kernel entropy and review the timing and kernel-stack concerns noted below. The auth-frame TX/RX hook remains to be written; H2E is not implemented.
3. **RSN IE and association.** AKM 00-0F-AC:8, MFPC and MFPR set in the RSN capabilities (WPA3 requires PMF), and the right group management cipher.
4. **4-way handshake changes for AKM 8.** KDF-SHA256 for the PTK, AES-128-CMAC for the EAPOL-Key MIC (key descriptor version 3), and the IGTK in the key data of message 3.
5. **PMF in the data path.** BIP for group management frames, and protected unicast deauth/disassoc. The hardware may or may not help; this is the part I know least about.
6. **Join log entries** for each step so a failure can be located (`Source/Driver/RTLJoinLog.h`, read with `Tools/test_uc joinlog`).

### Step 1 — SAE passphrase plumbing (ABI 2; built, not deployed)

- `RTL_RADIO_ABI` is now 2. `rtl_radio_join` appends an SAE marker, a passphrase length, and a 63-byte passphrase buffer; the existing WPA2 PMK fields and offsets are unchanged.
- `setWCL_ASSOCIATE` recognizes upper auth `0x1000`, accepts a non-empty password up to 63 bytes, and copies it into the join request. It continues to treat a 32-byte key as a WPA2 PMK for non-SAE joins.
- `rtl8814native_radio_join` logs only the password length, calls the supplicant's SAE placeholder, wipes its local copy and the ABI request copy, then returns `-EOPNOTSUPP`. The placeholder resets security state and logs that SAE is not implemented. This is plumbing only; WPA3 joins still fail cleanly.
- The user-client temporary join structure is wiped after the call. No password bytes are logged or written to the join log.
- Verification: `./build.sh`, `./build_airlayer.sh`, and the `Tools/test_uc` compile completed; the required vendor-symbol grep returned no matches. No files under `Source/rtl8814au/os_dep/linux/` were built. Nothing has been deployed.

### Step 2 — standalone SAE group 19 (built, not connected)

- `Source/Driver/SAE.c` and `SAE.h` implement P-256 and legacy hunt-and-peck PWE derivation, SHA-256/HMAC and the SAE KDF without kernel headers or corecrypto imports. I started with hunt-and-peck because it is the first requested path; H2E is not implemented, so H2E-only access points will not work.
- The API prepares a commit, includes a supplied anti-clogging token in a retry without changing the commit, validates peer scalar/point and reflection, derives KCK/PMK/PMKID, and sends/verifies confirms with a saturating send counter and replay check. Authentication frames and the anti-clogging status/group are the responsibility of the future TX/RX hook. `finish` copies the PMK and PMKID only after confirm and wipes the exchange context; the caller must wipe the output buffers after use. `clear` wipes an abandoned exchange. No key bytes are logged by the module or test.
- The caller must supply a cryptographically secure entropy callback. **Do not use `rtl_get_random_bytes` for SAE:** its current xorshift/timebase implementation is not cryptographically secure. There is no kernel entropy callback wired up yet and the existing join path still rejects SAE.
- The PWE loop runs at least 40 rounds (up to 200 if necessary), uses masked first-candidate selection and switches to a random dummy password and identifier after finding it. The P-256 multiplication uses a fixed-length ladder and masked point selection. This is an attempt to reduce timing leakage, not a side-channel audit: the generated code, compiler behavior and kernel stack usage have not been independently reviewed or measured. Hunt-and-peck itself has a history of side-channel attacks. Do not connect this to a production join path until those checks are done.
- Reference: the fixed group-19 vector in wpa_supplicant `src/common/common_module_tests.c`, branch `android10-qpr1-b-release`, labels its input IEEE P802.11-REVmd/D2.1 Annex J.10. The harness matches its local commit, KCK, PMK, PMKID and local confirm byte-for-byte and accepts its peer confirm. This is the vector as reproduced by wpa_supplicant, not an independent copy of the published 2020 standard. The test also checks a token retry, replay and invalid-input rejection, secret clearing and a two-party local exchange without password identifiers. It does not test a live hostapd session or the radio.
- Build and run the userland harness from the repository root:

  ```bash
  mkdir -p Build
  clang -DRTL_SAE_TESTING -std=c11 -Wall -Wextra -Werror -O2 -ISource/Driver \
      Source/Driver/SAE.c Tools/sae_test.c -o Build/sae_test
  ./Build/sae_test
  ```

  The deterministic scalar entry point is available only in the test build. The harness was also run with address and undefined-behavior sanitizers.
- `./build.sh` compiles the module into the kext without connecting it to the join path. The required unresolved vendor-symbol check was empty. `nm -u` on the SAE object showed only `memcpy` and `memset`; there are no corecrypto or extra arithmetic runtime imports. No `os_dep/linux` source was compiled and no deployment was performed.

## How I test

- A phone hotspot set to WPA3 (my router is locked, so I cannot force WPA3 or change its channel). The Wi-Fi password only ever goes into the macOS dialog, never into a command or a log.
- Build: `./build.sh` and `./build_airlayer.sh`; after the driver build `nm -u ... | grep -E "rtw|rtl|hal|dm_|odm|beam"` must print nothing. Never compile anything under `Source/rtl8814au/os_dep/linux/`.
- Deploy: `sudo ./Tools/phase5_deploy.sh "<internal EFI mount>" driver` (or `layer 2`), after checking `diskutil info` says Device Location: Internal. The disk number changes between reboots, and the same volume name can be a USB stick.
- Tracing macOS: the sources of the gates were found with `lldb -p $(pgrep -x airportd)` breakpoints on `Apple80211Associate2` (SIP debugging restrictions are off here), plus `dyld_info -arch x86_64 -disassemble` on IO80211.framework. DTrace cannot place offset probes in dyld-cache functions. IDA is the better tool where it can load the target.
- Keep a way to roll back: ethernet plugged in, and layer 0.28 and driver 4.22 backups in `Backups/`.

## Not covered here

WPA2/WPA3 Enterprise (802.1X) and channel-switch following are separate gaps, listed in `Docs/ROADMAP.md`.
