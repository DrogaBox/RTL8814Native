#!/usr/bin/env python3
"""
RTL8814Native — check OpenCore Kernel->Patch entries against the installed
macOS kernel (and the Apple kexts they target).

Run it after every macOS update, BEFORE rebooting into the new version if you
can (the update installs the new kernel on disk first):

    sudo diskutil mount disk1s1          # the 4.3 GB EFI; number changes per boot
    python3 Tools/check_kernel_patches.py
    python3 Tools/check_kernel_patches.py --config /path/to/config.plist

For every enabled patch that applies to this Darwin version it searches the
target binary the way OpenCore does (Find + Mask, optional Base/Limit) and
prints how many places match, which functions they are in, and which ones
OpenCore would actually patch (Skip / Count). A patch is flagged when:
  MISSING   no match: the patch is silently skipped
  SHORT     fewer matches than Count
  EXTRA     more matches than Count and no Base: OpenCore patches the first
            Count; check they are the function named in the comment (warning,
            does not fail the run). With a Base, the search starts at that
            symbol, so extra later matches are normal.
Why: on macOS 26.7.1 an AMD Vanilla "non-monotonic time" patch no longer hit
thread_invoke (offset moved to 0x490) and instead patched kperf and
vmx_hv_support, so the TSC panics came back (Docs/ARCHITECTURE.md, Milestone 0.1).

Exit code: 0 unless a patch is MISSING or SHORT.

Copyright (c) 2026 DrogaBox — GPL-2.0
"""
import argparse
import bisect
import glob
import os
import plistlib
import platform
import re
import struct
import subprocess
import sys

KERNEL = "/System/Library/Kernels/kernel"


def find_config():
    for c in sorted(glob.glob("/Volumes/*/EFI/OC/config.plist")):
        return c
    return None


def darwin_version():
    return tuple(int(x) for x in platform.release().split(".")[:3])


def ver(s):
    if not s:
        return None
    parts = [int(x) for x in s.split(".")] + [0, 0]
    return tuple(parts[:3])


def applies(p, kv):
    mn, mx = ver(p.get("MinKernel", "")), ver(p.get("MaxKernel", ""))
    return not ((mn and kv < mn) or (mx and kv > mx))


def kext_binary(bundle_id, cache={}):
    if not cache:
        for info in glob.glob("/System/Library/Extensions/*.kext/Contents/Info.plist") + \
                glob.glob("/System/Library/Extensions/*.kext/Contents/PlugIns/*.kext/Contents/Info.plist"):
            try:
                d = plistlib.load(open(info, "rb"))
            except Exception:
                continue
            exe = d.get("CFBundleExecutable")
            if exe:
                cache[d.get("CFBundleIdentifier")] = os.path.join(os.path.dirname(info), "MacOS", exe)
    return cache.get(bundle_id)


def text_range(data):
    """File range of the __TEXT_EXEC (or __TEXT) segment and its vmaddr."""
    if struct.unpack_from("<I", data, 0)[0] != 0xFEEDFACF:
        return 0, len(data), 0
    ncmds = struct.unpack_from("<I", data, 16)[0]
    off, segs = 32, {}
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            name = data[off + 8:off + 24].rstrip(b"\0").decode()
            vm, _, fo, fs = struct.unpack_from("<QQQQ", data, off + 24)
            segs[name] = (fo, fs, vm)
        off += size
    fo, fs, vm = segs.get("__TEXT_EXEC") or segs.get("__TEXT") or (0, len(data), 0)
    return fo, fo + fs, vm - fo


def symbols(path):
    out = subprocess.run(["nm", "-n", path], capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        a = line.split()
        if len(a) == 3 and a[1] in "tT":
            syms.append((int(a[0], 16), a[2]))
    return syms


def name_at(syms, addrs, v):
    i = bisect.bisect_right(addrs, v) - 1
    return f"{syms[i][1]}+{v - syms[i][0]:#x}" if i >= 0 else f"{v:#x}"


def pattern(find, mask):
    if not mask:
        return re.compile(re.escape(find), re.S)
    parts = []
    for f, m in zip(find, mask):
        if m == 0xFF:
            parts.append(re.escape(bytes([f])))
        elif m == 0:
            parts.append(b".")
        else:
            ok = bytes(b for b in range(256) if (b & m) == (f & m))
            parts.append(b"[" + b"".join(re.escape(bytes([b])) for b in ok) + b"]")
    return re.compile(b"".join(parts), re.S)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--config", help="config.plist (default: first mounted EFI)")
    ap.add_argument("--all", action="store_true", help="also list patches that do not apply to this Darwin")
    args = ap.parse_args()

    cfg = args.config or find_config()
    if not cfg or not os.path.exists(cfg):
        sys.exit("No config.plist found. Mount the EFI (sudo diskutil mount diskXs1) or pass --config.")
    kv = darwin_version()
    print(f"config : {cfg}")
    print(f"darwin : {'.'.join(map(str, kv))}  ({subprocess.run(['sw_vers', '-productVersion'], capture_output=True, text=True).stdout.strip()})")
    print()

    patches = plistlib.load(open(cfg, "rb"))["Kernel"]["Patch"]
    loaded = {}
    problems = warnings = 0
    for idx, p in enumerate(patches):
        comment = p.get("Comment", "")
        if not p.get("Enabled"):
            continue
        if not applies(p, kv):
            if args.all:
                print(f"[skip ] #{idx} {comment} (kernel range {p.get('MinKernel')}-{p.get('MaxKernel')})")
            continue
        ident = p.get("Identifier", "kernel")
        path = KERNEL if ident == "kernel" else kext_binary(ident)
        if not path or not os.path.exists(path):
            print(f"[?      ] #{idx} {comment}\n          {ident}: no binary on disk (on macOS 26 it only ships inside the kernel collection), not checked")
            continue
        if path not in loaded:
            data = open(path, "rb").read()
            lo, hi, slide = text_range(data)
            syms = symbols(path)
            loaded[path] = (data, lo, hi, slide, syms, [s[0] for s in syms])
        data, lo, hi, slide, syms, addrs = loaded[path]

        start, end = lo, hi
        base = p.get("Base", "")
        note = ""
        if base:
            if base != base.strip():
                note = f"Base {base!r} has stray whitespace (same in upstream AMD Vanilla); checked as {base.strip()!r}"
                base = base.strip()
            hit = [a for a, n in syms if n == base]
            if not hit:
                print(f"[MISSING] #{idx} {comment}\n          Base symbol {base} not found")
                problems += 1
                continue
            start = hit[0] - slide
            if p.get("Limit"):
                end = start + p["Limit"]

        rx = pattern(p["Find"], p.get("Mask") or b"")
        matches = [m.start() for m in rx.finditer(data, start, end)]
        count, skip = p.get("Count", 0), p.get("Skip", 0)
        applied = matches[skip:] if count == 0 else matches[skip:skip + count]
        want = count if count else None
        if not matches or not applied:
            tag = "MISSING"
        elif want and len(matches) - skip < want:
            tag = "SHORT"
        elif want and len(matches) - skip > want and not base:
            tag = "EXTRA"
        else:
            tag = "ok"
        if tag in ("MISSING", "SHORT"):
            problems += 1
        elif tag == "EXTRA":
            warnings += 1
        print(f"[{tag:7}] #{idx} {comment}")
        if note:
            print(f"          note: {note}")
        print(f"          {os.path.basename(path)}: {len(matches)} match(es), Skip={skip} Count={count}")
        for i, off in enumerate(matches[:8]):
            mark = "patched" if off in applied else "       "
            print(f"          {mark} {name_at(syms, addrs, off + slide)}")
        if len(matches) > 8:
            print(f"          ... {len(matches) - 8} more")

    print()
    if problems:
        print(f"{problems} patch(es) MISSING/SHORT: OpenCore skips them on this macOS. Fix before rebooting.")
    else:
        print("Every applicable patch finds its target.")
    if warnings:
        print(f"{warnings} EXTRA warning(s): confirm the 'patched' line is the function the comment names.")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
