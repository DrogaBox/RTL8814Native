#!/bin/zsh
# Verify Build/RTL8814AirLayer.kext against the 25.6.0 KC dumps:
#  1. every method RTLAirInterface / RTLAirController overrides sits in the
#     slot where Apple's own class implements the same method;
#  2. every symbol the kext imports exists in the KC (kernel + families);
#  3. IOWorkLoop::addEventSource is vtable +0x140, the slot Apple calls on
#     IO80211WorkQueue to add Skywalk queues.
set -e
D=${0:A:h}; ROOT=$D/../..
DUMP=${1:-$ROOT/Docs/re/io80211_25.6.0}
SDK=$(xcrun --sdk macosx --show-sdk-path)
T=$(mktemp -d)
CXXF=(-x c++ -std=c++17 -fapple-kext -fno-rtti -fno-exceptions -fno-use-cxa-atexit -mkernel
      -isysroot $SDK -DKERNEL -I$SDK/System/Library/Frameworks/Kernel.framework/Headers
      -I$ROOT/Source/IO80211 -I$ROOT/Source/Driver -Wno-inconsistent-missing-override -Wno-deprecated-declarations)
clang++ $CXXF -S $ROOT/Source/AirLayer/RTLAirLayer.cpp -o $T/a.s
python3 $D/check/vtcompare.py $T/a.s $DUMP RTLAirInterface=AppleBCMWLANSkywalkInterface RTLAirController=AppleBCMWLANCore
nm -u $ROOT/Build/RTL8814AirLayer.kext/Contents/MacOS/RTL8814AirLayer | sort -u > $T/u.txt
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
print("imports: %d symbols, %d missing from the KC" % (len(need), len(miss)))
for n in miss[:60]:
    print("  MISSING", n)
PY
cat > $T/wl.cpp <<'CPP'
#include <IOKit/IOWorkLoop.h>
struct P : IOWorkLoop { };
IOReturn (IOWorkLoop::*f)(IOEventSource *) = &IOWorkLoop::addEventSource;
CPP
clang++ $CXXF -S $T/wl.cpp -o $T/wl.s
echo "IOWorkLoop::addEventSource pointer-to-member (vtable offset + 1):"
grep -A2 "^__Z1f:\|^_f:" $T/wl.s | grep quad | head -1
rm -rf $T
