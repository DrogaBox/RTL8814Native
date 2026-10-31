#!/bin/bash
#
# Phase 5 EFI deploy, run with sudo on the mounted EFI.
#
#   sudo ./Tools/phase5_deploy.sh "<EFI mount>" driver    RTL8814Native 4.20
#   sudo ./Tools/phase5_deploy.sh "<EFI mount>" layer N [S] RTL8814AirLayer 0.15 + rtlair=N (1 or 2),
#                                                        rtlairstep=S if given (bisection, 1-4)
#   sudo RTL8814_AWDL=1 ./Tools/phase5_deploy.sh "<EFI mount>" layer 2 adds rtlawdl=1 (AWDL stage 1);
#   without it the arg is removed. Put the variable after sudo: sudo drops the caller's environment.
#   RTL8814_AWDL_TX=1 next to RTL8814_AWDL=1 also adds rtlawdl_tx=1 (AWDL action frame TX).
#   RTL8814_AWDL_MCC=1 (or 2) with them adds rtlawdl_mcc=N (AWDL while associated off the
#   social channels, needs a driver with rtlRadioAwdlOffch).
#   sudo ./Tools/phase5_deploy.sh "<EFI mount>" inert     remove rtlair from boot-args (layer stays, inert)
#
# Every run first copies EFI/OC/config.plist and the touched kexts to
# Backups/efi_phase5_<date>/. Ends with Tools/phase5_efi_check.py.
# Optional per-run source/checksum overrides (for a just-built test kext):
#   RTL8814_DRIVER_SRC=Build/RTL8814Native.kext RTL8814_DRIVER_MD5=<md5> ... driver
#   RTL8814_LAYER_SRC=Build/RTL8814AirLayer.kext RTL8814_LAYER_MD5=<md5> ... layer 2
#
set -euo pipefail
cd "$(dirname "$0")/.."

EFI="${1:?EFI mount path}"
STEP="${2:?driver | layer N | inert}"
OC="$EFI/EFI/OC"
CFG="$OC/config.plist"
PB=/usr/libexec/PlistBuddy
GUID="7C436110-AB2A-4BBB-A880-FE41995C9F82"
DRIVER_SRC="${RTL8814_DRIVER_SRC:-Build/RTL8814Native.kext}"
LAYER_SRC="${RTL8814_LAYER_SRC:-Build/RTL8814AirLayer.kext}"
DRIVER_MD5="${RTL8814_DRIVER_MD5:-$(md5 -q "$DRIVER_SRC/Contents/MacOS/RTL8814Native" 2>/dev/null || true)}"
LAYER_MD5="${RTL8814_LAYER_MD5:-$(md5 -q "$LAYER_SRC/Contents/MacOS/RTL8814AirLayer" 2>/dev/null || true)}"

[ "$(id -u)" = 0 ] || { echo "run with sudo (it writes to the EFI)"; exit 1; }
[ -f "$CFG" ] || { echo "no $CFG (is the EFI mounted?)"; exit 1; }
# Mount names move between boots ("NO NAME" can be the USB stick): internal disks only
diskutil info "$EFI" | grep -Eq 'Device Location: +Internal' || {
	echo "$EFI is not on an internal disk (USB stick?); mount the internal EFI and pass that path"
	diskutil info "$EFI" | grep -E 'Device Identifier|Protocol|Disk Size'
	exit 1; }

BK="Backups/efi_phase5_$(date +%Y%m%d_%H%M%S)_$$"
mkdir -p "$BK"
cp "$CFG" "$BK/config.plist"
for k in RTL8814Native.kext RTL8814AirLayer.kext; do
	[ -d "$OC/Kexts/$k" ] && cp -R "$OC/Kexts/$k" "$BK/"
done
echo "backup: $BK"

install_kext() {   # src md5
	local src="$1" want="$2" name exe got
	name="$(basename "$src")"
	exe="${name%.kext}"
	got="$(md5 -q "$src/Contents/MacOS/$exe")"
	[ "$got" = "$want" ] || { echo "$src has MD5 $got, expected $want"; exit 1; }
	rm -rf "$OC/Kexts/$name"
	cp -R "$src" "$OC/Kexts/$name"
	dot_clean -m "$OC/Kexts/$name"
	find "$OC/Kexts/$name" -name '._*' -delete
	echo "installed $name ($(md5 -q "$OC/Kexts/$name/Contents/MacOS/$exe" | cut -c1-8))"
}

kext_index() {     # bundle path -> index in Kernel:Add, or empty
	local i=0 bp
	while bp="$($PB -c "Print :Kernel:Add:$i:BundlePath" "$CFG" 2>/dev/null)"; do
		[ "$bp" = "$1" ] && { echo "$i"; return; }
		i=$((i + 1))
	done
}

set_rtlair() {     # value [step], or empty to remove both
	local args new
	args="$($PB -c "Print :NVRAM:Add:$GUID:boot-args" "$CFG" 2>/dev/null || true)"
	new="$(printf '%s\n' "$args" | tr ' ' '\n' | { grep -vE '^(rtlair(step)?|rtlawdl(_rx|_tx|_mcc)?)=' || true; } | tr '\n' ' ' | sed 's/ *$//')"
	[ -n "$1" ] && new="${new:+$new }rtlair=$1"
	[ -n "$1" ] && [ -n "${2:-}" ] && new="$new rtlairstep=$2"
	[ -n "$1" ] && [ "${RTL8814_AWDL:-0}" = 1 ] && new="$new rtlawdl=1"
	[ -n "$1" ] && [ "${RTL8814_AWDL:-0}" = 1 ] && [ "${RTL8814_AWDL_TX:-0}" = 1 ] && new="$new rtlawdl_tx=1"
	case "${RTL8814_AWDL_MCC:-0}" in 1|2)
		[ -n "$1" ] && [ "${RTL8814_AWDL:-0}" = 1 ] && [ "${RTL8814_AWDL_TX:-0}" = 1 ] &&
			new="$new rtlawdl_mcc=$RTL8814_AWDL_MCC" ;;
	esac
	$PB -c "Set :NVRAM:Add:$GUID:boot-args $new" "$CFG"
	echo "boot-args: $new"
}

case "$STEP" in
driver)
	install_kext "$DRIVER_SRC" "$DRIVER_MD5"
	;;
layer)
	N="${3:?rtlair value: 1 (scan only) or 2 (join + data)}"
	[ "$N" = 1 ] || [ "$N" = 2 ] || { echo "rtlair must be 1 or 2"; exit 1; }
	install_kext "$LAYER_SRC" "$LAYER_MD5"
	if [ -z "$(kext_index RTL8814AirLayer.kext)" ]; then
		nat="$(kext_index RTL8814Native.kext)"
		[ -n "$nat" ] || { echo "RTL8814Native.kext is not in Kernel -> Add"; exit 1; }
		i=$((nat + 1))
		$PB -c "Add :Kernel:Add:$i dict" \
		    -c "Add :Kernel:Add:$i:Arch string Any" \
		    -c "Add :Kernel:Add:$i:BundlePath string RTL8814AirLayer.kext" \
		    -c "Add :Kernel:Add:$i:Comment string Phase 5 native IO80211 layer" \
		    -c "Add :Kernel:Add:$i:Enabled bool true" \
		    -c "Add :Kernel:Add:$i:ExecutablePath string Contents/MacOS/RTL8814AirLayer" \
		    -c "Add :Kernel:Add:$i:MaxKernel string" \
		    -c "Add :Kernel:Add:$i:MinKernel string" \
		    -c "Add :Kernel:Add:$i:PlistPath string Contents/Info.plist" \
		    "$CFG"
		echo "config.plist: RTL8814AirLayer.kext added at Kernel -> Add $i"
	fi
	S="${4:-}"
	[ -z "$S" ] || [[ "$S" =~ ^[1-4]$ ]] || { echo "rtlairstep must be 1-4"; exit 1; }
	set_rtlair "$N" "$S"
	;;
inert)
	set_rtlair ""
	;;
*)
	echo "unknown step $STEP"; exit 1
	;;
esac

plutil -lint "$CFG"
sync
python3 Tools/phase5_efi_check.py "$EFI" || true
