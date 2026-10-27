#!/usr/bin/env python3
"""
Read-only check of a mounted OpenCore EFI before a Phase 5 test boot.

  python3 Tools/phase5_efi_check.py "/Volumes/NO NAME"

Reports which RTL8814Native / RTL8814AirLayer build is in EFI/OC/Kexts (by
MD5 of the binary), stray ._ files, the Kernel -> Add entries and their
order, and the rtlair boot-arg. Reads only those config.plist keys and
never writes anything.

Copyright (c) 2026 DrogaBox — GPL-2.0
"""
import hashlib
import os
import plistlib
import sys

KNOWN = {
    "39c5fdc2fe0e45e765cdf1a9ea86c596": "RTL8814Native 4.9",
    "9aed0533c96d7925d920095dff42b2a8": "RTL8814Native 4.10",
    "db46a680f8a21a9fb6461c922332d136": "RTL8814Native 4.11",
    "c90d00c5904f9a04b658c08be7560f31": "RTL8814Native 4.12 (4.11 + RTLPowerTrace)",
    "a1c7a9e076178dda18506f60d5d2c963": "RTL8814Native 4.13 (4.12 + own power desire)",
    "baf02980518b7c912ca05db66a788fd1": "RTL8814Native 4.14 (4.13 + no en2 with rtlair=2)",
    "67dabfd9c049760f4b33be06d6a86be5": "RTL8814Native 4.15 (4.14 + rtlRadioSetMac)",
    "cbba49b4dbc828e465f77ccb7bead815": "RTL8814Native 4.16 (4.15 + noise floor estimate in rtl_radio_status)",
    "efbd75e521661f7b1f6893ae3a16b85d": "RTL8814Native 4.17 (4.16 + noise hook fixed: physts_rpt_valid never set)",
    "2b23b33bf8fe5b1abf802b980da4028f": "RTL8814Native 4.18 (4.17 + join event log, kRTLUC_GetJoinLog / test_uc joinlog)",
    "501281301d917d6734948c07eb15cacd": "RTL8814Native 4.19 (4.18 + join log: auth TX, mgmt RX from the BSS, TXPAUSE/MSR/RCR)",
    "6258f28717b70e77c8d557c154a55497": "RTL8814Native 4.20 (4.19 + directed one-channel probe survey before each join)",
    "c5233bd91c7a226eb72a75f461a892ea": "RTL8814Native 4.21 (4.20 + flush scan queue on recent leave: full SSID survey)",
    "36ea1ac8a83a606ceeb2d36f8c9ff015": "RTL8814Native 4.22 (4.21 + REAUTH_LIMIT=32, MAX_JOIN_TIMEOUT=15000)",
    "04402631c59a4a756e356d299f8fd7e2": "RTL8814AirLayer 0.1 (superseded, do not use)",
    "206cf1337b555a57e7b6975301d1fa47": "RTL8814AirLayer 0.2 (superseded, do not use)",
    "92c7d5df31b667590564f8cd95af943c": "RTL8814AirLayer 0.3 (superseded, do not use)",
    "bc6dfde7eef40abe535862bd897c5001": "RTL8814AirLayer 0.4 (superseded, do not use)",
    "f1ccd8a5d41d6d216ffcbb9334f30c24": "RTL8814AirLayer 0.5 (superseded: panics in CCLogPipe::start)",
    "142270c9be2ed9e9d14d30e097e5a804": "RTL8814AirLayer 0.6 (superseded: panics in IONetworkController::start, getThread NULL)",
    "99e52166619bdf296e50b5b1cdc8026b": "RTL8814AirLayer 0.7 (superseded: class sizes wrong, panics in IO80211SkywalkInterface::start)",
    "e0a2384e6be2a5c75d2662c8d9b7d6e3": "RTL8814AirLayer 0.8 (superseded: starts, but radio RX/TX stall)",
    "7f1e244b97fcbeb6f5171a2a65f342f8": "RTL8814AirLayer 0.9 (rtlairstep bisection, superseded)",
    "89a2eb9317f35d118b725a64d1fe9e12": "RTL8814AirLayer 0.10 (bisection + call trace)",
    "4437d6aaa1b1c1eac6bc731c4837141a": "RTL8814AirLayer 0.11 (0.10 + own MAC for InterfaceNamer)",
    "b829354c9f091450a83f551dfbbc071f": "RTL8814AirLayer 0.12 (0.11 + APPLE80211_M_SCAN_DONE)",
    "531736a22f8f94884a6bb704473404df": "RTL8814AirLayer 0.13 (0.12 + posts inside the work queue gate)",
    "e4d99ba6381b114ca53165746e0e0b55": "RTL8814AirLayer 0.14 (0.13 + low latency info, radio MAC follows the interface)",
    "1c1f38ef960159a7c087ae404dca6039": "RTL8814AirLayer 0.15 (0.14 + extended BSS info, fast lane)",
    "c28620a0c70f36691d923f50bd6c7c8f": "RTL8814AirLayer 0.16 (0.15 + RX packets prepared before enqueue: fixes join KP)",
    "dee0b71572f66b21102b57f99c5db679": "RTL8814AirLayer 0.17 (0.16 + RX packets prepared without a source queue)",
    "d6c39b6528eccf3cddfdb81864b3b420": "RTL8814AirLayer 0.18 (0.17 + LQM reports: no missed-beacons leave)",
    "8253f70a534ca4d902f0713b81a6c05a": "RTL8814AirLayer 0.19 (0.18 + join trace, re-issue a join the radio dropped)",
    "6574226383787d5d6c45f922d00ed4c7": "RTL8814AirLayer 0.20 (0.19 + re-issue as soon as the radio drops the join)",
    "62bd08506bda145a253e33057642559f": "RTL8814AirLayer 0.21 (0.20 + WCL_REASSOC: roam to the best BSS of the network)",
    "aad245ea6161f1ac421f5ff1da7789ef": "RTL8814AirLayer 0.22 (0.21 + roam during a scan, WCL_SCAN_ABORT accepted)",
    "89e39b03aaca0eba02762b9497003e5a": "RTL8814AirLayer 0.23 (0.22 + MCS/NSS/country/beacon info/traffic counters/noise, WCL settings accepted)",
    "20fdc488e3e33deafc8efcfd527a2ae1": "RTL8814AirLayer 0.24 (0.23 + CHANNELS_INFO / COUNTRY_CHANNELS_INFO)",
    "f7eaf93518cc6ceba009aec30b1a754e": "RTL8814AirLayer 0.25 (0.24 + FW_HOT_CHANNELS, PLATFORM_CONFIG, SOI/real-time/BT coex accepted)",
    "613b72cbc45b205c86395ca253247790": "RTL8814AirLayer 0.26 (0.25 + setWCL_SET_ROAM_LOCK accepted)",
    "574f1f3aa4b0ea3eabe35caed3ab13e5": "RTL8814AirLayer 0.27 (0.26 + AWDL capability bits p[7]/p[8] in CARD_CAPABILITIES)",
}
KEXTS = [("RTL8814Native.kext", "RTL8814Native"), ("RTL8814AirLayer.kext", "RTL8814AirLayer")]
NVRAM_GUID = "7C436110-AB2A-4BBB-A880-FE41995C9F82"

problems = []


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    oc = os.path.join(sys.argv[1], "EFI", "OC")
    kdir = os.path.join(oc, "Kexts")
    if not os.path.isdir(kdir):
        sys.exit("no EFI/OC/Kexts under %s (is the EFI mounted?)" % sys.argv[1])

    print("== Kexts")
    for bundle, exe in KEXTS:
        p = os.path.join(kdir, bundle, "Contents", "MacOS", exe)
        if not os.path.exists(p):
            print("  %-22s not present" % bundle)
            continue
        h = md5(p)
        name = KNOWN.get(h, "unknown build")
        print("  %-22s %s  %s" % (bundle, h[:8], name))
        if "superseded" in name:
            problems.append("%s is a superseded build" % bundle)
        dots = [os.path.join(r, f) for r, _, fs in os.walk(os.path.join(kdir, bundle))
                for f in fs if f.startswith("._")]
        if dots:
            problems.append("%s has %d ._ files: run dot_clean -m on it" % (bundle, len(dots)))

    with open(os.path.join(oc, "config.plist"), "rb") as f:
        cfg = plistlib.load(f)

    print("== config.plist Kernel -> Add")
    add = cfg.get("Kernel", {}).get("Add", [])
    pos = {}
    for i, e in enumerate(add):
        bp = e.get("BundlePath", "")
        if bp in [b for b, _ in KEXTS]:
            pos[bp] = i
            print("  [%d] %s Enabled=%s ExecutablePath=%s MinKernel=%r" %
                  (i, bp, e.get("Enabled"), e.get("ExecutablePath"), e.get("MinKernel", "")))
            if not e.get("Enabled"):
                problems.append("%s is in Kernel -> Add but disabled" % bp)
    if "RTL8814Native.kext" not in pos:
        problems.append("RTL8814Native.kext missing from Kernel -> Add")
    layer_file = os.path.isdir(os.path.join(kdir, "RTL8814AirLayer.kext"))
    if "RTL8814AirLayer.kext" in pos:
        if not layer_file:
            problems.append("RTL8814AirLayer.kext is in config.plist but not in EFI/OC/Kexts")
        if pos["RTL8814AirLayer.kext"] < pos.get("RTL8814Native.kext", -1):
            problems.append("RTL8814AirLayer.kext must come after RTL8814Native.kext in Kernel -> Add")
    elif layer_file:
        print("  (RTL8814AirLayer.kext is in Kexts but not in config.plist: OpenCore ignores it)")

    print("== boot-args")
    nv = cfg.get("NVRAM", {})
    args = nv.get("Add", {}).get(NVRAM_GUID, {}).get("boot-args", "")
    if isinstance(args, bytes):
        args = args.decode(errors="replace")
    rtlair = [a for a in args.split() if a.startswith("rtlair")]
    print("  rtlair: %s" % (" ".join(rtlair) if rtlair else "not set (layer stays inert)"))
    if rtlair and "RTL8814AirLayer.kext" not in pos:
        problems.append("rtlair is set but the layer is not in Kernel -> Add")
    if "boot-args" not in nv.get("Delete", {}).get(NVRAM_GUID, []):
        problems.append("NVRAM -> Delete does not list boot-args: an edited boot-args may not take "
                        "effect (check `nvram boot-args` after boot)")

    print("== Result")
    if problems:
        for p in problems:
            print("  ! " + p)
        sys.exit(1)
    print("  OK")


if __name__ == "__main__":
    main()
