#!/bin/bash
#
# Builds Build/RTL8814AirLayer.kext, the Phase 5 native IO80211 layer
# (Source/AirLayer). Separate from ./build.sh: RTL8814Native.kext does not
# change. The layer is built against Source/IO80211/IO80211_25_6_0.h, so it
# only fits macOS 26.x / Darwin 25.6.0.
#
# Usage:  ./build_airlayer.sh
#
set -euo pipefail
cd "$(dirname "$0")"

ARCH="${ARCH:-x86_64}"
BUILD="Build"
OBJ="$BUILD/obj_airlayer"
KEXT="$BUILD/RTL8814AirLayer.kext"
BUNDLE_ID="com.DrogaBox.driver.RTL8814AirLayer"

SDK="$(xcrun --sdk macosx --show-sdk-path)"
SDK_VER="$(xcrun --sdk macosx --show-sdk-platform-version)"
KERN="$SDK/System/Library/Frameworks/Kernel.framework/Headers"

KCOMMON="-mkernel -isysroot $SDK -DKERNEL -I$KERN \
 -ISource/IO80211 -ISource/Driver \
 -fno-stack-protector -fno-builtin \
 -Wno-deprecated-declarations -Wno-inconsistent-missing-override"
CFLAGS="-x c -std=gnu11 $KCOMMON"
CXXFLAGS="-x c++ -std=c++17 -fapple-kext -fno-rtti -fno-exceptions -fno-use-cxa-atexit $KCOMMON"

rm -rf "$OBJ" "$KEXT"
mkdir -p "$OBJ" "$KEXT/Contents/MacOS"

echo "== RTL8814AirLayer build (arch $ARCH) =="
clang++ $CXXFLAGS -c Source/AirLayer/RTLAirLayer.cpp -o "$OBJ/RTLAirLayer.o"

cat << KMOD_EOF > "$OBJ/info.c"
#include <mach/mach_types.h>
#include <mach/kmod.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default")))
KMOD_EXPLICIT_DECL($BUNDLE_ID, "0.27.0", _start, _stop)

__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
KMOD_EOF
clang $CFLAGS -c "$OBJ/info.c" -o "$OBJ/info.o"

CLANG_RT="$(clang -print-resource-dir)/lib/darwin"
if [ -f "$CLANG_RT/libclang_rt.cc_kext.a" ]; then
    KEXT_LIB="-L$CLANG_RT -lclang_rt.cc_kext"
else
    KEXT_LIB="-lcc_kext"
fi
ld -kext -arch "$ARCH" \
   -platform_version macos "$SDK_VER" "$SDK_VER" \
   -o "$KEXT/Contents/MacOS/RTL8814AirLayer" \
   "$OBJ/info.o" "$OBJ/RTLAirLayer.o" \
   -L"$SDK/usr/lib" -lkmod $KEXT_LIB

cp Source/AirLayer/Info.plist "$KEXT/Contents/Info.plist"
codesign --force --sign - "$KEXT" >/dev/null 2>&1 || echo "   (codesign skipped)"
echo "Built: $KEXT"
