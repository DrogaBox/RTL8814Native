#!/usr/bin/env python3
"""Compare the vtables in a clang -S output with the KC dumps, slot by slot.
A stub (our class's own symbol) must sit where Apple's concrete class has its
own implementation; every other slot must name the exact same function."""
import re, subprocess, sys

asm, dump = sys.argv[1], sys.argv[2]
PAIRS = {"__ZTV11LayoutInfra": ("AppleBCMWLANSkywalkInterface", "LayoutInfra"),
         "__ZTV10LayoutCtrl": ("AppleBCMWLANCore", "LayoutCtrl")}
if len(sys.argv) > 3:                       # extra args: OurClass=AppleClass
    PAIRS = {}
    for a in sys.argv[3:]:
        ours, apple = a.split("=")
        PAIRS["__ZTV%d%s" % (len(ours), ours)] = (apple, ours)
lines = open(asm).read().split("\n")
tables, cur = {}, None
for l in lines:
    m = re.match(r"^(__ZTV\w+):", l)
    if m:
        cur = m.group(1) if m.group(1) in PAIRS else None
        if cur:
            tables[cur] = []
        continue
    if cur:
        m = re.match(r"\s+\.quad\s+(\S+)", l)
        if m:
            tables[cur].append(m.group(1))
        elif re.match(r"^\S", l) or ".section" in l:
            cur = None
bad = 0
for tv, (apple, ours) in PAIRS.items():
    if tv not in tables:
        bad += 1; print("%s: vtable not found" % ours); continue
    ent = tables[tv][2:]                    # skip offset-to-top and RTTI
    dem = subprocess.run(["c++filt", "-_"], input="\n".join(ent), capture_output=True,
                         text=True).stdout.split("\n")
    ref = [l.rstrip("\n").split(None, 2)[2] for l in open("%s/vt_%s.txt" % (dump, apple))]
    if ref and ref[-1] == "0":
        ref.pop()
    n = min(len(ref), len(dem))
    # Apple's concrete class may add slots past the IO80211 parent; only compare ours
    known = 0
    for i in range(len(dem)):
        if not dem[i] or dem[i] == "0":
            continue
        if "::~" in dem[i]:
            continue                         # destructors are per class
        if "_RESERVED" in dem[i] and i < len(ref) and dem[i] != ref[i] and \
                dem[i].split("::")[0] == ref[i].split("::")[0]:
            # the public SDK hides private slots of the same class as _RESERVED*;
            # the kext linker patches pad slots with the parent's entry
            known += 1
            print("%s slot %d: SDK pad %s, KC has %s (patched at link)" % (ours, i, dem[i], ref[i]))
            continue
        a = ref[i] if i < len(ref) else "<none>"
        d = dem[i]
        if d.startswith(ours + "::"):
            # our stub: Apple's class must implement this slot itself (same method name)
            if a.split("(")[0].split("::")[-1] != d.split("(")[0].split("::")[-1]:
                bad += 1; print("%s slot %d: ours %s / apple %s" % (ours, i, d, a))
        elif d != a:
            ao = a.split("::")[0]
            if ao.startswith("AppleBCMWLAN"):
                continue                     # Apple overrides an inherited slot; fine
            bad += 1; print("%s slot %d: %s / apple %s" % (ours, i, d, a))
    print("%s: %d slots, compared against %s (%d)" % (ours, len(dem), apple, len(ref)))
print("vtable mismatches: %d" % bad)
sys.exit(1 if bad else 0)
