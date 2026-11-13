# Review of stage 1 deployment (deploy and build)

Date: 2026-10-29. Static review, without touching the EFI or the tree: the deployment was not run and no
file was modified besides this report. Scope: `Tools/phase5_deploy.sh`, `build_airlayer.sh`,
`build.sh` and `Tools/phase5_efi_check.py`. The reviewed option is `RTL8814_AWDL=1`, which adds the
boot-arg `rtlawdl=1`; it exists only in the deploy — `build.sh` (RTL8814Native) contains no
reference to AWDL, and `build_airlayer.sh` compiles the AWDL path unconditionally but inertly without
the boot-arg, so activation requires an explicit deploy action and a boot action.

Method: the cases marked "measured" were reproduced with the shebang's interpreter (bash 3.2,
`set -euo pipefail`) and with PlistBuddy over a test plist, without access to the EFI; "?" marks what is not
verifiable without writing to the EFI.

## State after the verification

Applied in `Tools/phase5_deploy.sh`: issues 1 (with one clarification: the original boot-args filter, `^rtlair(step|awdl)?=`, did not match `rtlawdl=` and did not remove it, so a redeploy without the option accumulated the token instead of removing it; the pattern is now `^(rtlair(step)?|rtlawdl)=`, and the documented form is `sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh ...`), 2, 3 and 6. The new filter was measured over `rtlair=2 rtlawdl=1 keepsyms=1 -v` (result `keepsyms=1 -v`), over a list with only `rtlair*` tokens and over an empty list (without aborting). Issues 4, 5, 7, 8, 9 and 10 remain unapplied (low gravity).

## Issues

| # | Issue | Evidence | Severity | Proposed fix |
|---|---|---|---|---|
| 1 | The natural form `RTL8814_AWDL=1 sudo ./Tools/phase5_deploy.sh …` does not propagate the variable: sudo drops it from the environment (`env_reset`, ?) and, since it does not arrive, the filter of line 70 **removes** `rtlawdl` from boot-args. A plain redeploy disarms AWDL without any specific warning (it would only be detectable by reading the final `boot-args:` line) | `Tools/phase5_deploy.sh:3,8,70,73` | high | Document the form that does propagate: `sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh "<EFI>" layer 2`; add a warning on disarm (§Snippets, 1) |
| 2 | The default sources point to historical builds: driver 4.22 (`36ea1ac8…`) and pre-AWDL layer 0.30 (`d84740a6…`), while `Build/` holds 4.35 (`d96b50e1…`) and layer 0.32 with AWDL (`535c1767…`), and the deployed EFI runs 4.34 with a later layer. The documented commands without variables **silently downgrade the EFI**: the final check does not mark 4.22/0.30 as superseded, so no "!" appears. Measured: both default MD5s exist and match their pins — the problem is the skew, not integrity | `Tools/phase5_deploy.sh:26-29`; `Tools/phase5_efi_check.py:19-81` | medium | Default to `Build/` and derive the pin from the freshly built artifact (§Snippets, 2); an explicit pin still wins |
| 3 | `set_rtlair` aborts the script (after the backup, without writing anything) when all boot-args are `rtlair*` tokens: `grep -v` with no selected lines returns 1, `pipefail` propagates it to the `$( )` and `set -e` aborts (measured: `args="rtlair=2"` → exit 1 without message). It also cuts if the `boot-args` key does not exist: PlistBuddy's `Print` returns 1 (measured) | `Tools/phase5_deploy.sh:69-70` | medium | `\|\| true` on the `Print` and on the `grep` (§Snippets, 3) |
| 4 | Restoration is not documented: only the backup path is mentioned; the script offers no way back and there is no restore command in README.md nor in Docs/PHASE5_IO80211.md | `Tools/phase5_deploy.sh:12,38` (no other mentions) | medium | Add the single command of §Restoration to the header |
| 5 | The final check runs with `‖ true`: the script ends in 0 even if the check found problems (build superseded, `._` files, boot-args outside `NVRAM→Delete`, Kernel→Add order); the "!"s are printed but the exit status does not reflect them | `Tools/phase5_deploy.sh:116`; `Tools/phase5_efi_check.py:162` | low | `python3 Tools/phase5_efi_check.py "$EFI" \|\| echo "WARNING: phase5_efi_check reported problems (see above)"` |
| 6 | No root check: run without sudo, the script creates the local backup and dies afterward with a raw permission error on the first EFI write | `Tools/phase5_deploy.sh:17-44` | low | `[ "$(id -u)" = 0 ] \|\| { echo "run with sudo (it writes to the EFI)"; exit 1; }` after line 18 |
| 7 | On `layer` the kext is replaced before validating that RTL8814Native is in Kernel→Add: if missing, the EFI is left with the new layer installed and no config entry (OpenCore ignores it; the backup allows going back) | `Tools/phase5_deploy.sh:85-88` | low | Move the `kext_index` validation (86-101) before line 85's `install_kext` |
| 8 | `kext_index` takes the list as ended at an entry with empty `BundlePath` (the `while bp="$(…)"` uses the empty string as end): a false "not present" would add a duplicate entry. Edge case: all entries written by this script carry BundlePath | `Tools/phase5_deploy.sh:61` | low | `while $PB -c "Print :Kernel:Add:$i:BundlePath" "$CFG" 2>/dev/null | IFS= read -r bp; do` |
| 9 | The layer build does not print the binary's MD5, which is exactly the value the deploy's pin expects; the friction invites deploying the stale default of issue 2 | `build_airlayer.sh:72` | low | Add `md5 -q "$KEXT/Contents/MacOS/RTL8814AirLayer"` at the end |
| 10 | `ARCH` only reaches the linker; compilation uses the host's arch. Harmless with the fixed x86_64 target; a future `ARCH=arm64` would fail late, in `ld` | `build_airlayer.sh:15,39-40,64`; `build.sh:15,271` | informative | Pass `-arch "$ARCH"` to clang/clang++ in both scripts |

## Detailed snippets

1. Warning on `rtlawdl` disarm (insert in `set_rtlair`, after line 73):
   ```bash
   case " $args " in *" rtlawdl="*) [ "${RTL8814_AWDL:-0}" = 1 ] || \
       echo "note: rtlawdl removed (use: sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh ...)";; esac
   ```
2. Defaults to the freshly built artifact (replace lines 26-29; the derived MD5 stops
   skewing and the explicit pin by environment still takes priority):
   ```bash
   DRIVER_SRC="${RTL8814_DRIVER_SRC:-Build/RTL8814Native.kext}"
   LAYER_SRC="${RTL8814_LAYER_SRC:-Build/RTL8814AirLayer.kext}"
   DRIVER_MD5="${RTL8814_DRIVER_MD5:-$(md5 -q "$DRIVER_SRC/Contents/MacOS/RTL8814Native")}"
   LAYER_MD5="${RTL8814_LAYER_MD5:-$(md5 -q "$LAYER_SRC/Contents/MacOS/RTL8814AirLayer")}"
   ```
3. Boot-args filtering tolerant to lists with no remainder and to the missing key (replace lines 69-70):
   ```bash
   args="$($PB -c "Print :NVRAM:Add:$GUID:boot-args" "$CFG" 2>/dev/null || true)"
   new="$(printf '%s\n' "$args" | tr ' ' '\n' | { grep -vE '^rtlair(step|awdl)?=' || true; } | tr '\n' ' ' | sed 's/ *$//')"
   ```

## Verified correct

- **Backup before any EFI write**: `config.plist` and the two present kexts are
  copied to `Backups/efi_phase5_<date>_<pid>/` (lines 38-44) before `install_kext` (52-53) and before
  the boot-args `Set` (74). Inside `install_kext` the order is verify MD5 → delete → copy.
- The backup loop `[ -d … ] && cp -R …` looks like the classic `set -e` failure in a loop
  body and is not: measured with bash 3.2 (`set -ec`), the absence of one of the two kexts does not abort
  the script.
- **Boot-args fusion**: the filter only removes `rtlair=`, `rtlairstep=` and `rtlawdl=`; the rest of
  tokens are kept (measured: `rtlair=2 keepsyms=1 -v` → `keepsyms=1 -v` plus the re-added ones).
  Re-running with the same environment does not duplicate tokens, because the filter removes them before
  re-adding.
- **Idempotency**: repeated `driver` and `layer` produce the same EFI state (rm+cp with verified
  MD5; the Kernel→Add entry is only added if `kext_index` does not find it; resulting boot-args
  are deterministic). The backups accumulate one directory per run with date and PID,
  without collisions.
- **`rtlawdl` is not activated by accident**: only the literal value `RTL8814_AWDL=1` adds it
  (any other value, including absence, omits or removes it); `rtlair` is validated to {1,2} and
  `rtlairstep` to {1-4}; `inert` never adds it; and the kext re-checks it at runtime
  (`PE_parse_boot_argn("rtlawdl", …)`, `RTLAirLayer.cpp:1453-1457`) after the version gate.
  The `driver` step does not touch boot-args: a driver redeploy keeps `rtlawdl` and `rtlair`.
- Paths with spaces quoted in every expansion (works with "/Volumes/NO NAME") and
  internal disk guard before writing (32-36), which rejects an EFI on an external or USB disk.
- `set -euo pipefail` present in the three scripts; parameters handled with `${3:?}`/`${4:-}`;
  `plutil -lint` after the writes aborts, with a backup available; a `codesign` failure does not abort
  the build (explicit warning) in either script.
- `build.sh` contains no reference to AWDL nor to `RTL8814_AWDL`: the native driver does not change
  with the option. Consistent versions: KMOD `0.32.0` (`build_airlayer.sh:50`) =
  `CFBundleVersion` of `Source/AirLayer/Info.plist` (measured); the native keeps 0.1.0 in both.
- The layer build is fully regenerative (`rm -rf` of objects and kext) and ends with the
  known builds table regenerated (`airlayer_gate.py gen`), which aborts the build if the header
  or the table do not match.
- `Tools/phase5_efi_check.py` is read-only and covers: build superseded, `._` files, order and
  Enabled in Kernel→Add, layer in config without kext and in Kexts without config, `rtlair` without layer, and
  `boot-args` outside `NVRAM→Delete`.

## Restoration

With the EFI mounted and from the repo root, a single command returns the state prior to the last
deploy (the backup holds `config.plist` and the kexts that existed; restoring `config.plist`
also reverts the boot-args, `rtlair`/`rtlawdl` included; the prior deletion removes a kext
added by the deploy that was not in the backup):

```bash
sudo sh -c 'B="Backups/efi_phase5_<date>_<pid>"; E="<EFI>"; cp "$B/config.plist" "$E/EFI/OC/config.plist" && rm -rf "$E/EFI/OC/Kexts/RTL8814Native.kext" "$E/EFI/OC/Kexts/RTL8814AirLayer.kext" && cp -R "$B"/RTL8814*.kext "$E/EFI/OC/Kexts/"'
python3 Tools/phase5_efi_check.py "<EFI>"
```

After rebooting, it is worth verifying the restored boot-args took effect
(`nvram boot-args`); the check already warns if `boot-args` does not appear in `NVRAM→Delete`.
