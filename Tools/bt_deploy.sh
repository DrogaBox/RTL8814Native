#!/bin/bash
#
# Bluetooth dongle EFI deploy for AirDrop (BLE), run with sudo on the mounted EFI.
#
#   sudo ./Tools/bt_deploy.sh "<EFI mount>"            BlueToolFixup + the NVRAM keys
#   sudo RTL8814_BT_FORCE=1 ./Tools/bt_deploy.sh ...  also when no CSR dongle (0a12:0001) is plugged in
#
# What it writes (acidanthera BrcmPatchRAM README, macOS 12+):
#   - EFI/OC/Kexts/BlueToolFixup.kext and its Kernel -> Add entry right after Lilu (MinKernel 21.0.0)
#   - NVRAM Add and Delete, 7C436110-...: bluetoothExternalDongleFailed = <00>,
#     bluetoothInternalControllerInfo = 14 zero bytes
# It refuses to run next to an enabled BrcmBluetoothInjector, which BlueToolFixup replaces on macOS 12+.
# Every run first copies EFI/OC/config.plist and any BlueToolFixup.kext to Backups/efi_bt_<date>/.
# Kext source and checksum: RTL8814_BT_SRC / RTL8814_BT_MD5 (default: BrcmPatchRAM 2.7.2 RELEASE).

set -euo pipefail
cd "$(dirname "$0")/.."

EFI="${1:?EFI mount point}"
OC="$EFI/EFI/OC"
CFG="$OC/config.plist"
PB=/usr/libexec/PlistBuddy
GUID="7C436110-AB2A-4BBB-A880-FE41995C9F82"
SRC="${RTL8814_BT_SRC:-$HOME/re_cache/work/bt/BrcmPatchRAM-2.7.2/unzip/BlueToolFixup.kext}"
WANT="${RTL8814_BT_MD5:-2e4c0d25da74455dfa6522d1496ae0d3}"

[ "$(id -u)" = 0 ] || { echo "run with sudo (it writes to the EFI)"; exit 1; }
[ -f "$CFG" ] || { echo "no $CFG (is the EFI mounted?)"; exit 1; }
diskutil info "$EFI" | grep -Eq 'Device Location: +Internal' || {
	echo "$EFI is not on an internal disk (USB stick?); mount the internal EFI and pass that path"
	exit 1; }

# CSR8510 A10 (TP-Link UB400): vendor 0x0a12, product 0x0001
if ! python3 -c 'import plistlib, subprocess, sys
d = plistlib.loads(subprocess.check_output(["ioreg", "-a", "-p", "IOUSB", "-l", "-r", "-c", "IOUSBHostDevice"]))
sys.exit(0 if any(x.get("idVendor") == 0x0a12 and x.get("idProduct") == 1 for x in d) else 1)'; then
	echo "no CSR8510 (0a12:0001) on USB: plug the dongle into a motherboard port, not the monitor hub"
	[ "${RTL8814_BT_FORCE:-0}" = 1 ] || exit 1
	echo "RTL8814_BT_FORCE=1: going on without it"
fi

kext_index() {     # bundle path -> index in Kernel:Add, or empty
	local i=0 bp
	while bp="$($PB -c "Print :Kernel:Add:$i:BundlePath" "$CFG" 2>/dev/null)"; do
		[ "$bp" = "$1" ] && { echo "$i"; return; }
		i=$((i + 1))
	done
}

inj="$(kext_index BrcmBluetoothInjector.kext)"
if [ -n "$inj" ] && [ "$($PB -c "Print :Kernel:Add:$inj:Enabled" "$CFG")" = true ]; then
	echo "BrcmBluetoothInjector.kext is enabled at Kernel -> Add $inj; disable it first (not for macOS 12+)"
	exit 1
fi
lilu="$(kext_index Lilu.kext)"
[ -n "$lilu" ] || { echo "Lilu.kext is not in Kernel -> Add"; exit 1; }

got="$(md5 -q "$SRC/Contents/MacOS/BlueToolFixup")"
[ "$got" = "$WANT" ] || { echo "$SRC has MD5 $got, expected $WANT"; exit 1; }

BK="Backups/efi_bt_$(date +%Y%m%d_%H%M%S)_$$"
mkdir -p "$BK"
cp "$CFG" "$BK/config.plist"
[ -d "$OC/Kexts/BlueToolFixup.kext" ] && cp -R "$OC/Kexts/BlueToolFixup.kext" "$BK/"
echo "backup: $BK"

rm -rf "$OC/Kexts/BlueToolFixup.kext"
cp -R "$SRC" "$OC/Kexts/BlueToolFixup.kext"
dot_clean -m "$OC/Kexts/BlueToolFixup.kext"
find "$OC/Kexts/BlueToolFixup.kext" -name '._*' -delete
echo "installed BlueToolFixup.kext ($(md5 -q "$OC/Kexts/BlueToolFixup.kext/Contents/MacOS/BlueToolFixup" | cut -c1-8))"

if [ -z "$(kext_index BlueToolFixup.kext)" ]; then
	i=$((lilu + 1))
	$PB -c "Add :Kernel:Add:$i dict" \
	    -c "Add :Kernel:Add:$i:Arch string Any" \
	    -c "Add :Kernel:Add:$i:BundlePath string BlueToolFixup.kext" \
	    -c "Add :Kernel:Add:$i:Comment string Bluetooth on macOS 12+ (third-party dongle)" \
	    -c "Add :Kernel:Add:$i:Enabled bool true" \
	    -c "Add :Kernel:Add:$i:ExecutablePath string Contents/MacOS/BlueToolFixup" \
	    -c "Add :Kernel:Add:$i:MaxKernel string" \
	    -c "Add :Kernel:Add:$i:MinKernel string 21.0.0" \
	    -c "Add :Kernel:Add:$i:PlistPath string Contents/Info.plist" \
	    "$CFG"
	echo "config.plist: BlueToolFixup.kext added at Kernel -> Add $i (after Lilu)"
else
	$PB -c "Set :Kernel:Add:$(kext_index BlueToolFixup.kext):Enabled true" "$CFG"
	echo "config.plist: BlueToolFixup.kext already listed, enabled"
fi

plutil -replace "NVRAM.Add.$GUID.bluetoothExternalDongleFailed" -data AA== "$CFG"
plutil -replace "NVRAM.Add.$GUID.bluetoothInternalControllerInfo" -data AAAAAAAAAAAAAAAAAAA= "$CFG"
$PB -c "Print :NVRAM:Delete:$GUID" "$CFG" >/dev/null 2>&1 || $PB -c "Add :NVRAM:Delete:$GUID array" "$CFG"
for key in bluetoothExternalDongleFailed bluetoothInternalControllerInfo; do
	if ! $PB -c "Print :NVRAM:Delete:$GUID" "$CFG" | grep -qx "    $key"; then
		$PB -c "Add :NVRAM:Delete:$GUID: string $key" "$CFG"
	fi
done
echo "NVRAM: bluetoothExternalDongleFailed=00, bluetoothInternalControllerInfo=14 x 00 (Add + Delete)"

plutil -lint "$CFG"
sync
python3 Tools/phase5_efi_check.py "$EFI" || true
