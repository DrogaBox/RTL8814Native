#!/bin/bash
#
# Collects everything needed to judge a Phase 5 test boot into one file,
# without sudo: kext versions, boot-args, the driver and layer log since
# boot, IO80211 registry objects, interfaces and the radio status.
#
# Usage:  ./Tools/phase5_collect.sh          (writes Logs/phase5_<date>.txt)
#
# No Wi-Fi password is ever read or written here.
#
cd "$(dirname "$0")/.."
mkdir -p Logs
OUT="Logs/phase5_$(date +%Y%m%d_%H%M%S).txt"

section() { printf '\n===== %s =====\n' "$1"; }

{
	section "system"
	sw_vers
	uname -v
	section "boot-args"
	nvram boot-args 2>&1
	section "loaded kexts"
	kmutil showloaded 2>/dev/null | grep -E "RTL8814|IO80211|Skywalk" ||
		kextstat 2>/dev/null | grep -E "RTL8814|IO80211|Skywalk"
	section "RTL8814Native / RTL8814AirLayer log since boot"
	log show --last boot --style compact \
		--predicate 'eventMessage CONTAINS "RTL8814AirLayer" OR eventMessage CONTAINS "RTL8814Native"' 2>&1 | tail -400
	section "IO80211 / Skywalk log since boot (errors and interface)"
	log show --last boot --style compact \
		--predicate 'sender == "IO80211Family" OR sender == "IOSkywalkFamily"' 2>&1 |
		grep -iE "error|fail|RTLAir|attach|register|enable|panic" | tail -200
	section "registry: layer and interfaces"
	ioreg -l -w0 -r -c RTLAirController 2>&1 | head -200
	ioreg -l -w0 -r -c RTLAirInterface 2>&1 | head -200
	section "interfaces"
	ifconfig -a
	networksetup -listallhardwareports 2>&1
	section "radio (test_uc)"
	./Tools/test_uc radio abi 2>&1
	./Tools/test_uc radio status 2>&1
	section "airport state"
	system_profiler SPAirPortDataType 2>&1 | grep -vE "Network Password|Password" | head -80
	section "last panic, if any"
	ls -t /Library/Logs/DiagnosticReports/*panic* 2>/dev/null | head -3
} > "$OUT" 2>&1

echo "wrote $OUT"
