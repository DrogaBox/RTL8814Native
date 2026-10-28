#!/bin/bash
#
# RTL8814Native build script (macOS + Xcode CLT required)
#
# Builds Build/RTL8814Native.kext with plain clang/ld — no Xcode
# project needed. Vendor sources are NOT compiled until phase 1
# begins (VENDOR_SOURCES below stays empty for now).
#
# Usage:  ./build.sh          (build)
#         ./build.sh clean
#
set -euo pipefail
cd "$(dirname "$0")"

ARCH="${ARCH:-x86_64}"
BUILD="Build"
OBJ="$BUILD/obj"
KEXT="$BUILD/RTL8814Native.kext"
BUNDLE_ID="com.DrogaBox.driver.RTL8814Native"

SDK="$(xcrun --sdk macosx --show-sdk-path)"
SDK_VER="$(xcrun --sdk macosx --show-sdk-platform-version)"
KERN="$SDK/System/Library/Frameworks/Kernel.framework/Headers"

if [ ! -d "$KERN" ]; then
    echo "ERROR: Kernel framework headers not found at: $KERN"
    echo "Install Xcode (full) and try again."
    exit 1
fi

# ----------------------------------------------------------------------
# Vendor driver sources — populated per phase (see Docs/ROADMAP.md).
# Phase 1 starts with core/ + hal/ entry files; os_dep/linux/*.c are
# NEVER compiled (replaced by this kext + the Compat layer).
# ----------------------------------------------------------------------
VENDOR_SOURCES=(
    "Source/rtl8814au/hal/HalPwrSeqCmd.c"
    "Source/rtl8814au/hal/rtl8814a/Hal8814PwrSeq.c"
    "Source/rtl8814au/hal/rtl8814a/hal8814a_fw.c"
    "Source/rtl8814au/hal/hal_com.c"
    "Source/rtl8814au/hal/hal_com_phycfg.c"
    "Source/rtl8814au/hal/hal_phy.c"
    "Source/rtl8814au/hal/hal_dm.c"
    "Source/rtl8814au/hal/hal_intf.c"
    "Source/rtl8814au/hal/hal_hci/hal_usb.c"
    "Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c"
    "Source/rtl8814au/hal/rtl8814a/rtl8814a_phycfg.c"
    "Source/rtl8814au/hal/rtl8814a/rtl8814a_rf6052.c"
    "Source/rtl8814au/hal/rtl8814a/rtl8814a_dm.c"
    "Source/rtl8814au/hal/rtl8814a/rtl8814a_rxdesc.c"
    "Source/rtl8814au/hal/rtl8814a/rtl8814a_cmd.c"
    "Source/rtl8814au/hal/rtl8814a/rtl8814a_xmit.c"
    "Source/rtl8814au/hal/rtl8814a/rtl8814a_sreset.c"
    "Source/rtl8814au/hal/rtl8814a/usb/usb_halinit.c"
    "Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_led.c"
    "Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_xmit.c"
    "Source/rtl8814au/hal/rtl8814a/usb/rtl8814au_recv.c"
    "Source/rtl8814au/hal/efuse/rtl8814a/HalEfuseMask8814A_USB.c"
    "Source/rtl8814au/hal/led/hal_led.c"
    "Source/rtl8814au/hal/led/hal_usb_led.c"
    "Source/rtl8814au/hal/hal_btcoex.c"
    "Source/rtl8814au/hal/hal_btcoex_wifionly.c"
    "Source/rtl8814au/hal/hal_mp.c"
    "Source/rtl8814au/core/rtw_io.c"
    "Source/rtl8814au/core/rtw_cmd.c"
    "Source/rtl8814au/core/rtw_wlan_util.c"
    "Source/rtl8814au/core/rtw_security.c"
    "Source/rtl8814au/core/rtw_ieee80211.c"
    "Source/rtl8814au/core/efuse/rtw_efuse.c"
    "Source/rtl8814au/core/rtw_mem.c"
    "Source/rtl8814au/core/rtw_debug.c"
    "Source/rtl8814au/core/rtw_chplan.c"
    "Source/rtl8814au/core/rtw_vht.c"
    "Source/rtl8814au/core/rtw_rf.c"
    "Source/rtl8814au/core/rtw_sta_mgt.c"
    "Source/rtl8814au/core/rtw_pwrctrl.c"
    "Source/rtl8814au/core/rtw_sreset.c"
    "Source/rtl8814au/core/rtw_odm.c"
    "Source/rtl8814au/core/rtw_mi.c"
    "Source/rtl8814au/core/rtw_mlme.c"
    "Source/rtl8814au/core/rtw_mlme_ext.c"
    "Source/rtl8814au/core/rtw_mp.c"
    "Source/rtl8814au/core/rtw_btcoex.c"
    "Source/rtl8814au/core/rtw_btcoex_wifionly.c"
    "Source/rtl8814au/core/rtw_beamforming.c"
    "Source/rtl8814au/core/rtw_br_ext.c"
    "Source/rtl8814au/core/rtw_tdls.c"
    "Source/rtl8814au/core/rtw_p2p.c"
    "Source/rtl8814au/core/rtw_rm.c"
    "Source/rtl8814au/core/rtw_rm_fsm.c"
    "Source/rtl8814au/core/rtw_rm_util.c"
    "Source/rtl8814au/core/rtw_iol.c"
    "Source/rtl8814au/core/rtw_ioctl_query.c"
    "Source/rtl8814au/core/rtw_ioctl_set.c"
    "Source/rtl8814au/core/rtw_wapi.c"
    "Source/rtl8814au/core/rtw_wapi_sms4.c"
    "Source/rtl8814au/core/rtw_xmit.c"
    "Source/rtl8814au/core/rtw_recv.c"
    "Source/rtl8814au/hal/phydm/phydm.c"
    "Source/rtl8814au/hal/phydm/phydm_cck_pd.c"
    "Source/rtl8814au/hal/phydm/phydm_hwconfig.c"
    "Source/rtl8814au/hal/phydm/phydm_adaptivity.c"
    "Source/rtl8814au/hal/phydm/phydm_interface.c"
    "Source/rtl8814au/hal/phydm/phydm_rainfo.c"
    "Source/rtl8814au/hal/phydm/phydm_dig.c"
    "Source/rtl8814au/hal/phydm/phydm_math_lib.c"
    "Source/rtl8814au/hal/phydm/phydm_beamforming.c"
    "Source/rtl8814au/hal/phydm/phydm_primary_cca.c"
    "Source/rtl8814au/hal/phydm/rtl8814a/phydm_rtl8814a.c"
    "Source/rtl8814au/hal/phydm/rtl8814a/phydm_regconfig8814a.c"
    "Source/rtl8814au/hal/phydm/rtl8814a/halhwimg8814a_bb.c"
    "Source/rtl8814au/hal/phydm/rtl8814a/halhwimg8814a_mac.c"
    "Source/rtl8814au/hal/phydm/halrf/halrf.c"
    "Source/rtl8814au/hal/phydm/halrf/halrf_powertracking.c"
    "Source/rtl8814au/hal/phydm/halrf/halrf_powertracking_ce.c"
    "Source/rtl8814au/hal/phydm/halrf/halrf_kfree.c"
    "Source/rtl8814au/hal/phydm/halrf/rtl8814a/halrf_8814a_ce.c"
    "Source/rtl8814au/hal/phydm/halrf/rtl8814a/halhwimg8814a_rf.c"
    "Source/rtl8814au/hal/phydm/halrf/rtl8814a/halrf_iqk_8814a.c"
    "Source/rtl8814au/hal/phydm/txbf/haltxbf8814a.c"
    "Source/rtl8814au/hal/phydm/txbf/halcomtxbf.c"
    "Source/rtl8814au/hal/phydm/txbf/phydm_hal_txbf_api.c"
    "Source/rtl8814au/hal/phydm/phydm_adc_sampling.c"
    "Source/rtl8814au/hal/phydm/phydm_antdect.c"
    "Source/rtl8814au/hal/phydm/phydm_antdiv.c"
    "Source/rtl8814au/hal/phydm/phydm_api.c"
    "Source/rtl8814au/hal/phydm/phydm_auto_dbg.c"
    "Source/rtl8814au/hal/phydm/phydm_cck_rx_pathdiv.c"
    "Source/rtl8814au/hal/phydm/phydm_ccx.c"
    "Source/rtl8814au/hal/phydm/phydm_cfotracking.c"
    "Source/rtl8814au/hal/phydm/phydm_debug.c"
    "Source/rtl8814au/hal/phydm/phydm_dfs.c"
    "Source/rtl8814au/hal/phydm/phydm_direct_bf.c"
    "Source/rtl8814au/hal/phydm/phydm_dynamictxpower.c"
    "Source/rtl8814au/hal/phydm/phydm_lna_sat.c"
    "Source/rtl8814au/hal/phydm/phydm_mp.c"
    "Source/rtl8814au/hal/phydm/phydm_noisemonitor.c"
    "Source/rtl8814au/hal/phydm/phydm_pathdiv.c"
    "Source/rtl8814au/hal/phydm/phydm_phystatus.c"
    "Source/rtl8814au/hal/phydm/phydm_pmac_tx_setting.c"
    "Source/rtl8814au/hal/phydm/phydm_pow_train.c"
    "Source/rtl8814au/hal/phydm/phydm_psd.c"
    "Source/rtl8814au/hal/phydm/phydm_rssi_monitor.c"
    "Source/rtl8814au/hal/phydm/phydm_smt_ant.c"
    "Source/rtl8814au/hal/phydm/phydm_soml.c"
    "Source/rtl8814au/hal/phydm/halrf/halphyrf_ce.c"
    "Source/rtl8814au/hal/phydm/halrf/halrf_debug.c"
    "Source/rtl8814au/hal/phydm/halrf/halrf_psd.c"
    "Source/rtl8814au/hal/phydm/txbf/haltxbfinterface.c"
    "Source/rtl8814au/hal/phydm/txbf/haltxbfjaguar.c"
)

COMPAT_SOURCES=(
    "Source/Compat/Compat.c"
    "Source/Compat/VendorOsDep.c"
)

DRIVER_SOURCES=(
    "Source/Driver/RTL8814Native.cpp"
    "Source/Driver/RTLUsbDevice.cpp"
    "Source/Driver/RTLEthernetAdapter.cpp"
    "Source/Driver/RTLUserClient.cpp"
    "Source/Driver/VendorGlue.cpp"
    "Source/Driver/VendorProbe.c"
    "Source/Driver/WPA2Supplicant.c"
    "Source/Driver/SAE.c"
    "Source/Driver/SAEAuth.c"
    "Source/Driver/SAEJoin.c"
    "Source/Driver/PMF.c"
    "Source/Driver/CSA.c"
)

# Common kernel flags
#  -Wno-macro-redefined: IOLib.h redefines min/max after our shims in
#  mixed TUs — both bodies are equivalent, silence the noise.
#  -Wno-sign-conversion / -Wno-shorten-64-to-32: Xcode 26 clang enables
#  these for -mkernel builds; a Linux-compat layer legitimately mixes
#  int/unsigned/size_t (Linux idiom), so the noise is not actionable.
#  (C++ only: -Wno-inconsistent-missing-override sits in CXXFLAGS —
#  Apple's own IOUSBHostFamily headers don't mark overrides.)
KCOMMON="-mkernel -isysroot $SDK -DKERNEL \
 -DCONFIG_RTL8814A \
 -DCONFIG_LITTLE_ENDIAN \
 -DDM_ODM_SUPPORT_TYPE=0x04 \
 -I$KERN \
 -ISource/Compat/include \
 -ISource/Driver \
 -ISource/rtl8814au/include \
 -ISource/rtl8814au/hal/phydm \
 -fno-stack-protector -fno-builtin \
 -Wno-deprecated-declarations -Wno-macro-redefined \
 -Wno-sign-conversion -Wno-shorten-64-to-32 \
 -Wno-pointer-sign -Wno-shadow -Wno-implicit-int-conversion \
 -Wno-address-of-packed-member -Wno-parentheses-equality"

CFLAGS="-x c -std=gnu11 $KCOMMON"
CXXFLAGS="-x c++ -std=c++17 -fapple-kext -fno-rtti -fno-exceptions -fno-use-cxa-atexit $KCOMMON -Wno-inconsistent-missing-override"

case "${1:-build}" in
clean)
    rm -rf "$BUILD"
    echo "cleaned."
    exit 0
    ;;
esac

mkdir -p "$OBJ" "$KEXT/Contents/MacOS"

NEWEST_HDR="$(find Source/Compat/include Source/Driver -name '*.h' -o -name '*.hpp' | xargs ls -t 2>/dev/null | head -n 1 || true)"

compile() {
    local src="$1"; local force="${2:-0}"; local ext="${src##*.}"
    local obj="$OBJ/$(echo "$src" | tr '/' '_').o"
    if [ "$force" = "1" ] || [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || { [ -n "$NEWEST_HDR" ] && [ "$NEWEST_HDR" -nt "$obj" ]; }; then
        echo "  CC  $src"
        if [ "$ext" = "c" ]; then
            clang $CFLAGS -c "$src" -o "$obj"
        else
            clang++ $CXXFLAGS -c "$src" -o "$obj"
        fi
    fi
    echo "$obj" >> "$BUILD/objects.txt"
}

echo "== RTL8814Native build (arch $ARCH) =="

rm -f "$BUILD/objects.txt"
touch "$BUILD/objects.txt"

echo "-- compat layer"
for f in "${COMPAT_SOURCES[@]}"; do compile "$f" 1; done

echo "-- driver (IOKit)"
for f in "${DRIVER_SOURCES[@]}"; do compile "$f" 1; done

if [ ${#VENDOR_SOURCES[@]} -gt 0 ] && [ -n "${VENDOR_SOURCES[0]}" ]; then
    echo "-- vendor radio (ported files)"
    for f in "${VENDOR_SOURCES[@]}"; do compile "$f"; done
else
    echo "-- vendor radio: no sources yet (phase 0)"
fi

echo "-- generating kmod info"
INFO_C="$OBJ/RTL8814Native_info.c"
cat << KMOD_EOF > "$INFO_C"
#include <mach/mach_types.h>
#include <mach/kmod.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default")))
KMOD_EXPLICIT_DECL($BUNDLE_ID, "0.1.0", _start, _stop)

__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
KMOD_EOF
clang $CFLAGS -c "$INFO_C" -o "$OBJ/RTL8814Native_info.o"

echo "-- link"
CLANG_RT="$(clang -print-resource-dir)/lib/darwin"
if [ -f "$CLANG_RT/libclang_rt.cc_kext.a" ]; then
    KEXT_LIB="-L$CLANG_RT -lclang_rt.cc_kext"
else
    KEXT_LIB="-lcc_kext"
fi

ld -kext -arch "$ARCH" \
   -platform_version macos "$SDK_VER" "$SDK_VER" \
   -o "$KEXT/Contents/MacOS/RTL8814Native" \
   "$OBJ/RTL8814Native_info.o" \
   $(cat "$BUILD/objects.txt") \
   -L"$SDK/usr/lib" -lkmod \
   $KEXT_LIB

cp Source/Driver/Info.plist "$KEXT/Contents/Info.plist"

echo "-- codesign (ad-hoc)"
codesign --force --sign - "$KEXT" >/dev/null 2>&1 || \
    echo "   (codesign skipped — sign manually if needed)"

echo ""
echo "Built: $KEXT"
echo ""
echo "Load for testing (SIP kext checks must be off):"
echo "  sudo kmutil load -p $PWD/$KEXT"
echo "Watch the log:"
echo "  log stream --predicate 'eventMessage CONTAINS \"RTL8814Native\"'"
echo ""
echo "Boot install (OpenCore): copy the kext to EFI/OC/Kexts and add"
echo "to config.plist -> Kernel -> Add (MinKernel empty)."
