#!/bin/zsh
# Verify Source/IO80211/IO80211_<build>.h against the KC dumps:
#  1. the vtables of a stub subclass of IO80211InfraProtocol / IO80211Controller
#     match Apple's slot by slot (Apple's concrete classes as reference);
#  2. every IO80211/Skywalk symbol the stub object imports exists in the KC.
# usage: Tools/re/check_io80211.sh [dump_dir]   (needs syms_*.txt from kcsyms.py in dump_dir)
set -e
D=${0:A:h}; ROOT=$D/../..
DUMP=${1:-$ROOT/Docs/re/io80211_25.6.0}
SDK=$(xcrun --sdk macosx --show-sdk-path)
T=$(mktemp -d)
CXXF=(-x c++ -std=c++17 -fapple-kext -fno-rtti -fno-exceptions -fno-use-cxa-atexit -mkernel
      -isysroot $SDK -DKERNEL -I$SDK/System/Library/Frameworks/Kernel.framework/Headers
      -I$ROOT/Source/IO80211 -Wno-inconsistent-missing-override -Wno-deprecated-declarations)
clang++ $CXXF -S $D/check/layout_test.cpp -o $T/l.s
clang++ $CXXF -c $D/check/layout_test.cpp -o $T/l.o
python3 $D/check/vtcompare.py $T/l.s $DUMP
nm -u $T/l.o | grep -E "IO80211|IOSkywalk" | sort -u > $T/u.txt
python3 - $T/u.txt $DUMP <<'PY'
import sys, glob
need = [l.strip() for l in open(sys.argv[1]) if l.strip()]
have = set()
for f in glob.glob(sys.argv[2] + "/syms*.txt"):
    for l in open(f):
        p = l.split()
        if len(p) == 3 and p[1] != "01":
            have.add(p[2])
miss = [n for n in need if n not in have]
print("imports: %d IO80211/Skywalk symbols, %d missing from the KC" % (len(need), len(miss)))
for n in miss[:40]:
    print("  MISSING", n)
sys.exit(1 if miss else 0)
PY
rm -rf $T
