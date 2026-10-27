# WPA3-SAE: where it stands and what is left

Notes for whoever picks this up. Everything below was checked on my one setup (TP-Link Archer T9UH, macOS 26.7.1, OpenCore). Read `Docs/PHASE5_IO80211.md` (section "WPA3-SAE gates") first; this file is the to-do list that follows it.

## State

- Driver 4.22 and layer 0.30 are what is in my EFI right now. The layer is the 0.28 code plus three capability bits (`Source/AirLayer/RTLAirLayer.cpp`, `getCARD_CAPABILITIES`): `p[13] |= 0x08` (capability 75, SAE), `p[10] |= 0x10` (PMF), on top of the AWDL bits.
- With those bits macOS offers SAE on WPA3 networks, and `setWCL_ASSOCIATE` runs. For a WPA3-only hotspot it receives:
  - `key + 4` (key length) = the passphrase length, and the passphrase bytes at `key + 16`. For WPA2 it was always 32 (a PMK).
  - `req + 16` = 1 and `req + 20` = 0x1000 (open-system lower auth, SAE upper auth).
- The layer then refuses it (`keyLen != 32` returns `kIOReturnUnsupported`), so the join fails after a few milliseconds. `RTLAirLastAssoc` in ioreg keeps the header of the last request (no key bytes).
- Side effect: macOS now also tries SAE on my WPA2/WPA3 mixed router, and that join fails. Layer 0.28 (`Backups/airlayer_0.28`, md5 `7066099f...`) does not have the bits and keeps the router working. To go back, point `LAYER_SRC`/`LAYER_MD5` in `Tools/phase5_deploy.sh` at it and deploy.

## What the vendor code gives us

- `Source/rtl8814au/core/rtw_ieee80211.c` knows the SAE AKM selectors (00-0F-AC:8 and :9) and nothing more. There is no SAE logic in the vendor tree.
- `OnAuthClient` and `issue_auth` in `core/rtw_mlme_ext.c` are the places to hook the auth exchange. `OnAuthClient` has a cfg80211 branch for SAE that is not compiled here (we have no cfg80211). `issue_auth` always builds an open-system or shared-key frame, so SAE needs its own sender with algorithm 3 and an arbitrary body.
- `core/rtw_security.c` has BIP/IGTK code behind `CONFIG_IEEE80211W`. That flag is not set in `build.sh`. Turning it on has not been tried and may pull in files we do not build.
- `Source/Driver/WPA2Supplicant.c` is the in-kernel supplicant: self-contained SHA-1/HMAC-SHA1/PBKDF2, PRF-384, AES key unwrap, the 4-way handshake and the key install into the CAM. It only does AKM 2 (PSK) with key descriptor version 2.

## What has to be written

1. **Plumbing.** `struct rtl_radio_join` (`Source/Driver/RTLRadioABI.h`) has no way to carry a passphrase. Add an SAE flag and a passphrase field at the end and bump `RTL_RADIO_ABI`. In the layer (`setWCL_ASSOCIATE`) accept `keyLen != 32` when the upper auth is SAE and copy the passphrase through. In the driver, `rtl8814native_radio_join` (`Source/Driver/VendorProbe.c`) and `rtl8814_wpa2_configure` need an SAE branch. Wipe the passphrase after use, as the PMK is wiped today.
2. **SAE exchange** (IEEE 802.11-2020, section 12.4): group 19 (P-256), password element derivation (hunt-and-peck is enough to start; the mixed hotspot may require H2E, check its RSNXE), commit and confirm frames as auth algorithm 3 sequence 1 and 2, anti-clogging token handling, and the PMK and PMKID out of it. This needs bignum and EC arithmetic in the kernel. The kernel already exports corecrypto (`com.apple.kec.corecrypto`, loaded on this machine); I have not checked which `ccec_*`/`ccn_*` symbols a third-party kext can link, and OpenCore drops a kext with unresolved symbols, so check with `nm -u` as the README says.
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

## How I test

- A phone hotspot set to WPA3 (my router is locked, so I cannot force WPA3 or change its channel). The Wi-Fi password only ever goes into the macOS dialog, never into a command or a log.
- Build: `./build.sh` and `./build_airlayer.sh`; after the driver build `nm -u ... | grep -E "rtw|rtl|hal|dm_|odm|beam"` must print nothing. Never compile anything under `Source/rtl8814au/os_dep/linux/`.
- Deploy: `sudo ./Tools/phase5_deploy.sh "<internal EFI mount>" driver` (or `layer 2`), after checking `diskutil info` says Device Location: Internal. The disk number changes between reboots, and the same volume name can be a USB stick.
- Tracing macOS: the sources of the gates were found with `lldb -p $(pgrep -x airportd)` breakpoints on `Apple80211Associate2` (SIP debugging restrictions are off here), plus `dyld_info -arch x86_64 -disassemble` on IO80211.framework. DTrace cannot place offset probes in dyld-cache functions. IDA is the better tool where it can load the target.
- Keep a way to roll back: ethernet plugged in, and layer 0.28 and driver 4.22 backups in `Backups/`.

## Not covered here

WPA2/WPA3 Enterprise (802.1X) and channel-switch following are separate gaps, listed in `Docs/ROADMAP.md`.
