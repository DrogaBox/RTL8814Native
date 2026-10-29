#!/usr/bin/env python3
"""Recheck the symbols the AWDL stage 1 code imports against symbol dumps.

The requirement list is extracted automatically from the AirLayer sources:

  * every __asm("__ZN...") label in Source/AirLayer/AWDLInterface.cpp and
    Source/AirLayer/RTLAirLayer.cpp (direct C imports, matched by exact name);
  * every virtual method declared in the IO80211VirtualInterface block of
    Source/AirLayer/AWDLInterface.h, whose family implementation the loader
    must resolve to fill the awdl0 vtable. The mangled name is built as the
    length-prefixed class + method prefix (Itanium), so the parameter types
    never have to be encoded; a dump symbol matches when it continues the
    prefix with the end-of-nested-name marker 'E'. Pure declarations
    (= 0) are skipped: the kext implements those, the family exports nothing.

Usage:
    python3 Tools/awdl_recheck_symbols.py DUMP [DUMP ...] [--extra-list FILE]
                                            [--sources DIR]

    DUMP            symbol dump files in the Docs/re/io80211_<version>/
                    format: one symbol per line, "<address> <size> <name>",
                    hex addresses, e.g.
                      Docs/re/io80211_25.6.0/syms.txt
                      Docs/re/io80211_25.6.0/syms_com.apple.iokit.IOSkywalkFamily.txt
                      Docs/re/io80211_25.6.0/syms_com.apple.driver.corecapture.txt
                      Docs/re/io80211_25.6.0/syms_com.apple.kernel.txt
                      Docs/re/io80211_25.6.0/syms_com.apple.iokit.IONetworkingFamily.txt
                    (pass every dump of the version: the CoreCapture symbols live in
                    their own dump; with all five, 25.6.0 gives 128 of 128 OK)
                    A line with address 0000000000000000 is an undefined
                    reference of that module, not a definition.
    --extra-list    optional text file with one full mangled symbol per line
                    (# comments and blank lines ignored), for requirements the
                    sources cannot express.
    --sources       directory holding AWDLInterface.cpp/.h and RTLAirLayer.cpp
                    (default: <repo>/Source/AirLayer).

Output: one table row per required symbol (symbol, requirement kind, dump and
address where defined, entry type, status), then a summary. Exit status is 1
when any required symbol is not defined in any dump, 0 otherwise, 2 on usage
or input errors.
"""

import argparse
import bisect
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
AIRLAYER_DEFAULT = REPO_ROOT / "Source" / "AirLayer"
ASM_SOURCES = ("AWDLInterface.cpp", "RTLAirLayer.cpp")
VIF_HEADER = "AWDLInterface.h"
VIF_CLASS = "IO80211VirtualInterface"

# An __asm("...") label attached to a declaration: the exact symbol the
# object file will reference.
ASM_LABEL_RE = re.compile(r'__asm\(\s*"([^"\n]+)"\s*\)')

# Inside a class block: a virtual method declaration, terminated by the first
# ';' after the 'virtual' keyword (no default arguments with ';' appear there).
VIRTUAL_KW_RE = re.compile(r"\bvirtual\b")
METHOD_NAME_RE = re.compile(r"(\w+)\s*\(")
PURE_TAIL_RE = re.compile(r"\)\s*=\s*0\s*$")


def read_text(path):
    if not path.is_file():
        sys.exit("error: missing input file: %s" % path)
    return path.read_text(encoding="utf-8", errors="replace")


def asm_requirements(airlayer_dir):
    """{symbol: [source file names]} for every __asm label in the cpp files."""
    reqs = {}
    for fname in ASM_SOURCES:
        for m in ASM_LABEL_RE.finditer(read_text(airlayer_dir / fname)):
            label = m.group(1)
            if label:
                reqs.setdefault(label, []).append(fname)
    return reqs


def class_block(text, class_name):
    """Source text of a class/struct body, braces excluded."""
    m = re.search(r"\b(?:struct|class)\s+" + re.escape(class_name) + r"\b[^{;]*\{", text)
    if not m:
        sys.exit("error: %s not found as a class definition" % class_name)
    depth, i = 1, m.end()
    while i < len(text) and depth:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    if depth:
        sys.exit("error: unbalanced braces in the %s block" % class_name)
    return text[m.end(): i - 1]


def vtable_requirements(airlayer_dir):
    """Mangled name expectations for the virtual methods of VIF_CLASS.

    Returns ({expectation: kind}, [skipped pure method names]) where
    expectation is the mangled prefix '__ZN<len><class><len><method>' and kind
    records the origin. Parameters are not encoded: a dump symbol satisfies
    the expectation when it continues the prefix with 'E'.
    """
    body = class_block(read_text(airlayer_dir / VIF_HEADER), VIF_CLASS)
    class_enc = "__ZN%d%s" % (len(VIF_CLASS), VIF_CLASS)
    reqs, pure = {}, []
    for m in VIRTUAL_KW_RE.finditer(body):
        semi = body.find(";", m.end())
        if semi < 0:
            break
        decl = body[m.end():semi].strip()
        nm = METHOD_NAME_RE.search(decl)
        if not nm:
            continue
        name = nm.group(1)
        if PURE_TAIL_RE.search(decl):
            pure.append(name)
            continue
        expectation = "%s%d%s" % (class_enc, len(name), name)
        reqs.setdefault(expectation, "vtable:" + VIF_HEADER)
    return reqs, pure


def extra_list_requirements(path):
    """{full mangled symbol: kind} from the optional list file."""
    reqs = {}
    for lineno, line in enumerate(read_text(path).splitlines(), 1):
        entry = line.split("#", 1)[0].strip()
        if not entry:
            continue
        if not re.fullmatch(r"__?Z\w+", entry):
            sys.exit("error: %s:%d: not a mangled symbol: %s" % (path, lineno, entry))
        reqs.setdefault(entry, "list:" + path.name)
    return reqs


def load_dump(path):
    """Parse a dump into (label, defined{name: addr}, sorted_defined, undefined)."""
    defined, undefined = {}, set()
    for lineno, line in enumerate(read_text(path).splitlines(), 1):
        parts = line.split()
        if not parts:
            continue
        if len(parts) != 3:
            sys.exit("error: %s:%d: expected '<address> <size> <name>', got: %s"
                     % (path, lineno, line))
        addr, _size, name = parts
        if addr.strip("0") == "":
            undefined.add(name)
        else:
            defined[name] = addr
    return path.name, defined, sorted(defined), undefined


def prefix_matches(names, addresses, prefix):
    """Defined symbols starting with prefix whose next char is 'E'."""
    hits = []
    i = bisect.bisect_left(names, prefix)
    while i < len(names) and names[i].startswith(prefix):
        if names[i][len(prefix):len(prefix) + 1] == "E":
            hits.append((names[i], addresses[names[i]]))
        i += 1
    return hits


def main():
    ap = argparse.ArgumentParser(
        description="Check AWDL symbol imports against kernel symbol dumps.")
    ap.add_argument("dumps", nargs="+", metavar="DUMP",
                    help="symbol dump file (Docs/re/io80211_<version> format)")
    ap.add_argument("--extra-list", metavar="FILE",
                    help="optional file with extra full mangled symbols")
    ap.add_argument("--sources", metavar="DIR", type=Path, default=AIRLAYER_DEFAULT,
                    help="directory with AWDLInterface.cpp/.h, RTLAirLayer.cpp "
                         "(default: %(default)s)")
    args = ap.parse_args()

    asm = asm_requirements(args.sources)
    vt, pure = vtable_requirements(args.sources)
    extra = extra_list_requirements(Path(args.extra_list)) if args.extra_list else {}

    # Merge the three requirement kinds per expectation. An asm or list entry
    # is an exact symbol; a vtable entry is a prefix + 'E'. A symbol can carry
    # both origins (e.g. init and start are imported and also fill vtable
    # cells); the origins are reported together.
    order = {"asm": 0, "vtable": 1, "list": 2}
    reqs = {}
    for sym, sources in asm.items():
        reqs.setdefault(sym, {"exact": sym, "prefix": None, "kinds": set()})["kinds"].add(
            "asm:" + "+".join(sources))
    for expectation, kind in vt.items():
        entry = reqs.setdefault(expectation, {"exact": expectation, "prefix": None,
                                              "kinds": set()})
        entry["prefix"] = expectation
        entry["exact"] = None  # the asm-exact form is covered by prefix + 'E'
        entry["kinds"].add(kind)
    for sym, kind in extra.items():
        entry = reqs.setdefault(sym, {"exact": sym, "prefix": None, "kinds": set()})
        if entry["prefix"] is None:
            entry["exact"] = sym
        entry["kinds"].add(kind)

    dumps = [load_dump(Path(p)) for p in args.dumps]

    rows, missing = [], 0
    for expectation in sorted(reqs, key=lambda s: (min(order[k.split(":")[0]]
                                                      for k in reqs[s]["kinds"]), s)):
        req = reqs[expectation]
        def_hits, undef_hits = [], []
        for label, defined, sorted_defined, undefined in dumps:
            if req["prefix"]:
                hits = prefix_matches(sorted_defined, defined, req["prefix"])
                if not hits and any(n.startswith(req["prefix"])
                                    and n[len(req["prefix"]):len(req["prefix"]) + 1] == "E"
                                    for n in undefined):
                    undef_hits.append(label)
            elif req["exact"] in defined:
                hits = [(req["exact"], defined[req["exact"]])]
            else:
                hits = []
                if req["exact"] in undefined:
                    undef_hits.append(label)
            def_hits.extend((label, addr, name) for name, addr in hits)

        kinds = "+".join(sorted(req["kinds"], key=lambda k: (order[k.split(":")[0]], k)))
        if def_hits:
            found = "; ".join("%s@%s" % (l, a) for l, a, _ in def_hits)
            entry_type, status = "defined", "OK"
        elif undef_hits:
            found = "; ".join(undef_hits) + " (addr 0)"
            entry_type, status = "undefined-ref", "MISSING"
            missing += 1
        else:
            found, entry_type, status = "-", "absent", "MISSING"
            missing += 1
        rows.append((expectation, kinds, found, entry_type, status))

    sym_w = max([len(r[0]) for r in rows] + [len("SYMBOL")])
    kind_w = max([len(r[1]) for r in rows] + [len("KIND")])
    dump_w = max([len(r[2]) for r in rows] + [len("FOUND IN")])
    print("AirLayer sources: %s (asm labels: %d, %s vtable methods: %d, "
          "pure skipped: %s, extra list: %d)"
          % (args.sources, len(asm), VIF_CLASS, len(vt),
             ",".join(pure) if pure else "none", len(extra)))
    print("Dumps: %s" % ", ".join(d[0] for d in dumps))
    print()
    print("%-*s  %-*s  %-*s  %-13s  %s" % (sym_w, "SYMBOL", kind_w, "KIND",
                                           dump_w, "FOUND IN", "TYPE", "STATUS"))
    for sym, kinds, found, entry_type, status in rows:
        print("%-*s  %-*s  %-*s  %-13s  %s" % (sym_w, sym, kind_w, kinds,
                                               dump_w, found, entry_type, status))
    print()
    print("checked: %d requirements, %d OK, %d MISSING" % (len(rows),
                                                           len(rows) - missing, missing))
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
