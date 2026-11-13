#!/bin/bash
#
# AWDL test with a real iPhone (Docs/AWDL_PRUEBA_CON_IPHONE.md).
#
#   Tools/iphone_test.sh check          state of the Mac, no root, changes nothing
#   sudo Tools/iphone_test.sh run       collect everything until Ctrl-C
#   Tools/iphone_test.sh report <dir>   summary of a run
#   sudo Tools/iphone_test.sh deploy    Build/ driver and layer to the internal EFI (reboot after it)
#   sudo Tools/iphone_test.sh channel 6|44|149|auto
#                                       parked channel preferred while not associated (reboot after it)
#
# A run writes to ~/Desktop/iphone_<date>/ and needs no network. Its files carry
# the iPhone's name and addresses, so they stay out of the repository.
#
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT="$PWD"

DRIVER_BIN=Build/RTL8814Native.kext/Contents/MacOS/RTL8814Native
LAYER_BIN=Build/RTL8814AirLayer.kext/Contents/MacOS/RTL8814AirLayer
PASSED=1
# The layer sets it when it creates awdl0: the family's 1484 does not fit its TX ring and
# the excess is lost as "bad", with no error to the stack (Docs/AWDL_A53_SNIFFER.md §14).
AWDL_MTU=1428

ok()  { printf 'ok    %s\n' "$*"; }
bad() { printf 'MISSING %s\n' "$*"; PASSED=0; }
inf() { printf '      %s\n' "$*"; }

uuid_of()   { dwarfdump --uuid "$1" 2>/dev/null | awk '{print $2}' | head -1; }
uuid_kext() { kextstat -b "$1" 2>/dev/null | awk -v b="$1" '$0 ~ b {print $8}' | head -1; }
awdl_mtu()  { ifconfig awdl0 2>/dev/null | awk 'NR == 1 {print $NF}'; }
# EUI-64 link-local address of a MAC, as ndp prints it
eui64_ll()  { local IFS=:; set -- $1; printf 'fe80::%x:%x:%x:%x' $(( (0x$1 ^ 2) << 8 | 0x$2 )) \
	$(( 0x$3 << 8 | 0xff )) $(( 0xfe00 | 0x$4 )) $(( 0x$5 << 8 | 0x$6 )); }

# Every line of the layer's ring in awdl0, oldest first
awdl_ring() {
	ioreg -a -r -c RTLAwdInterface 2>/dev/null | python3 -I -c '
import plistlib, sys
data = sys.stdin.buffer.read()
def walk(o):
    if isinstance(o, list):
        for x in o:
            yield from walk(x)
    elif isinstance(o, dict):
        if "RTLAwdlLog" in o:
            yield o["RTLAwdlLog"]
        else:
            yield from walk(o.get("IORegistryEntryChildren", []))
if data:
    for ring in walk(plistlib.loads(data)):
        print("\n".join(str(x) for x in ring))
        break
'
}

# The iPhone's side, when it is on USB and its owner tapped Trust: its log (log collect)
# and the IP on its interfaces (rvictl). Lockdown services, no Developer Mode needed.
# Optional: the run goes on without them.
as_owner() {
	if [ "$(id -u)" = 0 ] && [ -n "${SUDO_USER:-}" ]; then sudo -u "$SUDO_USER" "$@"; else "$@"; fi
}

iphone_usb() {   # udid, pairing, name, iOS, model of the first iPhone on USB
	local j
	j="$(mktemp -t iphone)"
	[ "$(id -u)" = 0 ] && [ -n "${SUDO_USER:-}" ] && chown "$SUDO_USER" "$j"
	as_owner xcrun devicectl list devices --json-output "$j" >/dev/null 2>&1
	python3 -I -c '
import json, sys
try:
    devs = json.load(open(sys.argv[1]))["result"]["devices"]
except Exception:
    devs = []
for d in devs:
    hw = d.get("hardwareProperties", {})
    c = d.get("connectionProperties", {})
    p = d.get("deviceProperties", {})
    if hw.get("deviceType") != "iPhone" or c.get("transportType") != "wired":
        continue
    print("\t".join(str(x) for x in (hw.get("udid", ""), c.get("pairingState", ""),
        p.get("name", ""), p.get("osVersionNumber", ""), hw.get("productType", ""))))
    break
' "$j"
	rm -f "$j"
}

last_kernel_line() {   # text
	/usr/bin/log show --last 2m --predicate "process == \"kernel\" AND eventMessage CONTAINS \"$1\"" 2>/dev/null |
		tail -1 | sed 's/.*com.apple.p2p.awdl0: //'
}

cmd_check() {
	local build boot args want a
	build="$(sw_vers -buildVersion)"
	[ "$build" = 25G241 ] && ok "macOS $build" || bad "macOS $build: the offsets are for 25G241"

	for pair in "com.DrogaBox.driver.RTL8814Native $DRIVER_BIN" "com.DrogaBox.driver.RTL8814AirLayer $LAYER_BIN"; do
		set -- $pair
		a="$(uuid_kext "$1")"; want="$(uuid_of "$2")"
		if [ -z "$a" ]; then bad "$1 is not loaded"
		elif [ "$a" = "$want" ]; then ok "${1##*.} loaded, $a"
		else bad "${1##*.} loaded $a, Build/ has $want: deploy and reboot pending"
		fi
	done

	args="$(nvram boot-args 2>/dev/null | cut -f2-)"
	for want in rtlair=2 rtlawdl=1 rtlawdl_tx=1 rtlawdl_mcc=1 rtlawdl_sim=1; do
		case " $args " in *" $want "*) ;; *) bad "boot-arg $want missing" ;; esac
	done
	case " $args " in *" rtlawdl_ch="*) inf "boot-arg $(printf '%s\n' $args | /usr/bin/grep rtlawdl_ch)" ;; esac
	for a in $(printf '%s\n' $args | /usr/bin/grep -E '^rtlawdl_(air|lastaw|dense|rate|ratemax|ba)='); do inf "boot-arg $a"; done

	if [ -x Tools/test_uc ] && [ ! Tools/test_uc.c -nt Tools/test_uc ] && /usr/bin/grep -q "capture self-test" Tools/test_uc; then
		ok "test_uc has the capture"
	else
		bad "Tools/test_uc is older than its source: clang -Wall -O2 Tools/test_uc.c -framework IOKit -framework CoreFoundation -o Tools/test_uc"
	fi
	pgrep -q -x awdlsim && bad "awdlsim is running: a real peer test needs it stopped"

	a="$(system_profiler SPBluetoothDataType 2>/dev/null)"
	printf '%s\n' "$a" | /usr/bin/grep -q 'State: On' && ok "Bluetooth on" || bad "Bluetooth off"
	/usr/bin/log show --last boot --predicate 'process == "bluetoothd" AND eventMessage CONTAINS "Overriding Multiple Advertising"' 2>/dev/null |
		/usr/bin/grep -q 'disabled' && ok "BLE preference in force" || bad "BLE preference not seen in this boot (LE SupportsMultipleAdvertising)"
	[ "$(/usr/bin/log show --last boot --predicate 'process == "bluetoothd" AND eventMessage CONTAINS "STATUS 712"' 2>/dev/null | /usr/bin/grep -c 'STATUS 712')" = 0 ] &&
		ok "no BLE scan error 712" || bad "BLE scan error 712 in this boot"

	a="$(defaults read com.apple.sharingd DiscoverableMode 2>/dev/null)"
	[ "$a" = Everyone ] && ok "AirDrop: Everyone" || bad "AirDrop: ${a:-unknown}, the test needs Everyone"

	# ifconfig exits non-zero on awdl0 (SIOCGIFXMEDIA) with the interface fine.
	a="$(ifconfig awdl0 2>/dev/null | awk '/ether/ {print $2}')"
	[ -n "$a" ] && ok "awdl0 exists, $a" || bad "no awdl0"
	a="$(awdl_mtu)"
	[ "$a" = "$AWDL_MTU" ] && ok "awdl0 MTU $a" || bad "awdl0 MTU ${a:-unknown}, not $AWDL_MTU: the layer should set it"
	a="$(awdl_ring | /usr/bin/grep 'awdl: tx psf' | tail -1)"
	if [ -n "$a" ]; then
		inf "$a"
		printf '%s\n' "$a" | /usr/bin/grep -q 'mif [0-9]* fail 0 ' || bad "PSF/MIF with TX failures"
	else
		inf "no TX line yet: AWDL has not sent 1024 frames since this boot"
	fi
	a="$(last_kernel_line 'AWDL ON')"
	[ -n "$a" ] && inf "$a"
	# Without these a peer takes the Mac for one with no HT or VHT (AWDL_CAPS_ADVERTISED §6).
	if /usr/bin/log show --last 2m --predicate 'process == "kernel" AND eventMessage CONTAINS "Invalid _htCapabilityLength"' 2>/dev/null |
		/usr/bin/grep -q 'Invalid _htCapabilityLength'; then
		bad "the manager leaves TLV 7 out of its templates (Invalid _htCapabilityLength)"
	else
		ok "no HT template error in the last 2 min"
	fi

	# From the manager's line: SPAirPortDataType scans for 9 s, longer than a peer stands
	# silence (Docs/AWDL_PRUEBA_CON_IPHONE.md).
	a="$(last_kernel_line 'AWDL ON' | sed -n 's/.*infra(\([0-9]*\)).*/\1/p')"
	[ "$a" = 0 ] && a=""
	case "$a" in
	"") inf "en2 not associated: the radio parks on the sequence's social channel, the safest case" ;;
	6|44|149) ok "en2 on channel $a: social, no need to leave the network" ;;
	*) inf "en2 on channel $a: not social, time sharing; leaving the network is the safest case, and costs the internet if en1 has no link" ;;
	esac
	a="$(ifconfig en1 2>/dev/null | awk '/status:/ {print $2}')"
	inf "en1: ${a:-no link}; default route on $(route -n get default 2>/dev/null | awk '/interface:/ {print $2}')"

	a="$(iphone_usb)"
	if [ -z "$a" ]; then
		inf "no iPhone on USB: the run records the Mac's side only"
	else
		set -- $(printf '%s\n' "$a" | cut -f2,4,5 | tr '\t' ' ')
		case "$1" in
		paired) ok "iPhone on USB and trusted ($3, iOS $2): the run also takes its log and IP" ;;
		*) inf "iPhone on USB, pairing '$1': Trust on the iPhone is pending" ;;
		esac
	fi

	a="$(df -g "$HOME/Desktop" | awk 'NR==2 {print $4}')"
	[ "${a:-0}" -ge 2 ] && ok "$a GB free" || bad "only ${a:-0} GB free"
	echo
	[ "$PASSED" = 1 ] && echo "READY" || echo "NOT READY"
	[ "$PASSED" = 1 ]
}

# OpenCore's own EFI partition, OPENCORE, on the 1 TB disk since 2026-11-08. Without it,
# the only internal EFI partition, which CachyOS shares. The disk identifier moves between
# boots and the USB stick also holds an EFI folder called NO NAME: only internal physical
# disks qualify.
efi_dev() {
	local ids
	ids="$(diskutil list internal physical | awk '$2 == "EFI" && $3 == "OPENCORE" {print $NF}')"
	[ -n "$ids" ] || ids="$(diskutil list internal physical | awk '$2 == "EFI" {print $NF}')"
	[ "$(printf '%s\n' "$ids" | /usr/bin/grep -c .)" = 1 ] || { echo "internal EFI partitions: [$ids], expected one"; return 1; }
	printf '%s\n' "$ids"
}

efi_mount() {
	[ "$(id -u)" = 0 ] || { echo "needs sudo"; exit 1; }
	local id mp
	id="$(efi_dev)" || { printf '%s\n' "$id"; exit 1; }
	mp="$(diskutil info "$id" | sed -n 's/^ *Mount Point: *//p')"
	if [ -z "$mp" ]; then
		mkdir -p /Volumes/EFI && diskutil mount -mountPoint /Volumes/EFI "$id" >&2 || exit 1
		mp=/Volumes/EFI
	fi
	[ -f "$mp/EFI/OC/config.plist" ] || { echo "$id at $mp has no EFI/OC/config.plist"; exit 1; }
	printf '%s\n' "$mp"
}

# A FAT left mounted here until the reboot lost a directory entry twice after a CachyOS update
# wrote to the shared partition (config.plist on 10-28, the layer's kext on 11-08): flush and
# unmount it as soon as the copy ends.
efi_release() {   # mount point
	local id
	id="$(efi_dev)"
	sync
	diskutil unmount "$1" >/dev/null 2>&1 || echo "could not unmount $1: unmount it before rebooting"
	diskutil verifyVolume "$id" 2>&1 | /usr/bin/grep -E 'exit code|Orphan|Warning'
}

deploy_layer() {   # mount point, rtlawdl_ch value or empty
	# RTL8814_AWDL_EXTRA (layer tunables) passes through from the caller: sudo VAR=... deploy
	RTL8814_AWDL=1 RTL8814_AWDL_TX=1 RTL8814_AWDL_MCC=1 RTL8814_AWDL_SIM=1 RTL8814_AWDL_CH="$2" \
		Tools/phase5_deploy.sh "$1" layer 2
}

cmd_deploy() {
	local mp ch
	mp="$(efi_mount)" || { printf '%s\n' "$mp"; exit 1; }
	echo "internal EFI at $mp"
	Tools/phase5_deploy.sh "$mp" driver || exit 1
	# A channel set before stays set.
	ch="$(nvram boot-args 2>/dev/null | tr ' \t' '\n\n' | sed -n 's/^rtlawdl_ch=//p')"
	deploy_layer "$mp" "$ch" || exit 1
	efi_release "$mp"
	echo "pending: reboot"
}

cmd_channel() {
	local mp ch="${1:-}"
	case "$ch" in 6|44|149|auto) ;; *) echo "channel 6, 44, 149 or auto"; exit 1 ;; esac
	mp="$(efi_mount)" || { printf '%s\n' "$mp"; exit 1; }
	echo "internal EFI at $mp"
	[ "$ch" = auto ] && ch=""
	deploy_layer "$mp" "$ch" || exit 1
	efi_release "$mp"
	echo "pending: reboot"
}

cmd_run() {
	[ "$(id -u)" = 0 ] || { echo "needs sudo: sudo $0 run"; exit 1; }
	pgrep -q -x awdlsim && { echo "awdlsim is running; a real peer test needs it stopped"; exit 1; }
	local owner dir mtu pids=""
	owner="${SUDO_USER:-$(stat -f %Su "$ROOT")}"
	dir="$(eval echo "~$owner")/Desktop/iphone_$(date +%Y%m%d_%H%M%S)"
	mkdir -p "$dir" || exit 1
	cat > "$dir/README.txt" <<-EOF
	Run of Tools/iphone_test.sh. Summary: Tools/iphone_test.sh report "$dir"
	frames.pcapng  AWDL frames both ways at the radio, 802.11, direction and RSSI in each packet's comment
	awdl0_ip.pcap  IP traffic on awdl0
	system.log     kernel AWDL lines, sharingd, wifip2pd, bluetoothd
	ring.txt       the layer's RTLAwdlLog, new lines every 5 s
	state_*.txt    interfaces and neighbours at both ends
	iphone_*       only with an iPhone on USB and trusted: its log (iphone.logarchive, filtered
	               in iphone_system.log) and the IP on its interfaces (iphone_ip.pcap, PKTAP)
	EOF

	mtu="$(awdl_mtu)"
	[ "$mtu" = "$AWDL_MTU" ] && ok "awdl0 MTU $mtu" || bad "awdl0 MTU ${mtu:-unknown}, not $AWDL_MTU: full TCP segments to the iPhone will be lost"

	snapshot() {
		{
			date; sw_vers -buildVersion; nvram boot-args
			ifconfig awdl0; ifconfig en2; ndp -an; netstat -rn -f inet6 | /usr/bin/grep -i awdl
			system_profiler SPBluetoothDataType 2>/dev/null
			kextstat 2>/dev/null | /usr/bin/grep -E 'DrogaBox|BlueToolFixup'
			ls -lt "$(eval echo "~$owner")/Downloads" | head -15
		} > "$dir/state_$1.txt" 2>&1
	}
	snapshot start

	# Two files for the Mac to iPhone direction
	printf 'AirDrop test from the Mac, %s\n' "$(date)" > "$dir/envio_chico.txt"
	yes 'AirDrop test from the Mac, one megabyte of filler.' | head -c 1048576 > "$dir/envio_1mb.txt"

	local iphone udid="" rvi="" t0
	t0="$(date -v-1M '+%Y-%m-%d %H:%M:%S')"
	iphone="$(iphone_usb)"
	if [ -n "$iphone" ]; then
		printf '%s\n' "$iphone" > "$dir/iphone.txt"
		case "$(printf '%s\n' "$iphone" | cut -f2)" in
		paired)
			udid="$(printf '%s\n' "$iphone" | cut -f1)"
			ok "iPhone on USB: its log is collected at Ctrl-C"
			rvictl -s "$udid" > "$dir/rvictl.txt" 2>&1
			rvi="$(sed -n 's/.*interface \(rvi[0-9]*\).*/\1/p' "$dir/rvictl.txt" | head -1)"
			if [ -n "$rvi" ]; then
				tcpdump -i "$rvi" -n -s 0 -U -w "$dir/iphone_ip.pcap" > "$dir/tcpdump_iphone.txt" 2>&1 &
				pids="$pids $!"
				ok "iPhone IP on $rvi"
			else
				inf "rvictl gave no interface: $(tr '\n' ' ' < "$dir/rvictl.txt" | cut -c1-120)"
			fi
			;;
		*) inf "iPhone on USB without Trust: Mac side only" ;;
		esac
	fi

	caffeinate -dimsu -w $$ & pids="$pids $!"
	/usr/bin/log stream --style compact --level info --predicate \
		'(process == "kernel" AND (eventMessage CONTAINS "awdl" OR eventMessage CONTAINS "AWDL" OR eventMessage CONTAINS "RTL8814")) OR process == "sharingd" OR process == "wifip2pd" OR process == "bluetoothd" OR process == "rapportd" OR (process == "mDNSResponder" AND (eventMessage CONTAINS[c] "awdl" OR eventMessage CONTAINS "D2D" OR eventMessage CONTAINS "_airdrop" OR eventMessage CONTAINS "_applicationservicepairing" OR eventMessage CONTAINS "_appsvcprepair" OR eventMessage CONTAINS "_asquic"))' \
		> "$dir/system.log" 2>&1 & pids="$pids $!"
	tcpdump -i awdl0 -n -s 0 -U -w "$dir/awdl0_ip.pcap" > "$dir/tcpdump.txt" 2>&1 & pids="$pids $!"
	(
		seen="$dir/.ring_seen"; : > "$seen"
		while :; do
			awdl_ring > "$dir/.ring_now"
			# The ring repeats lines and rotates: only what this run has not written yet.
			/usr/bin/grep -vxFf "$seen" "$dir/.ring_now" | while IFS= read -r l; do
				printf '%s %s\n' "$(date +%H:%M:%S)" "$l"
			done >> "$dir/ring.txt"
			sort -u -o "$seen" "$seen" "$dir/.ring_now"
			sleep 5
		done
	) & pids="$pids $!"
	# The family installs no neighbour entry for an AWDL peer on this layer
	# (2026-11-08: the iPhone's SYN stayed in SYN_RCVD until ndp -s): a
	# permanent one for each source the radio hears, at its EUI-64 address.
	(
		while :; do
			/usr/bin/sed -n 's/.* HEARD \([0-9a-f:]\{17\}\) .*/\1/p' "$dir/capture.txt" 2>/dev/null | sort -u |
			while read -r mac; do
				ll="$(eui64_ll "$mac")"
				ndp -an | /usr/bin/grep -q "^$ll%awdl0 " && continue
				ndp -s "$ll%awdl0" "$mac" > /dev/null 2>&1 &&
					printf '[%s] ndp: %s%%awdl0 -> %s\n' "$(date +%H:%M:%S)" "$ll" "$mac"
			done
			sleep 1
		done
	) & pids="$pids $!"
	# What the system says, as it says it, next to the radio's lines
	python3 -I -c '
import re, sys, time
f = open(sys.argv[1], errors="replace")
f.seek(0, 2)
state = None
ble = 0
seen = {}
def say(text):
    print("[%s] %s" % (time.strftime("%H:%M:%S"), text), flush=True)
while True:
    line = f.readline()
    if not line:
        time.sleep(0.3)
        continue
    m = re.search(r"peers:(\d+), cached:(\d+)", line)
    if m and m.groups() != state:
        state = m.groups()
        say("IO80211 peers %s, cached %s" % state)
    if "Found BLE device" in line and time.time() - ble > 15:
        ble = time.time()
        say("sharingd: BLE device found")
    if "AirDrop Bonjour server ready" in line:
        say("sharingd: AirDrop server ready")
    # The Mac side of discovery (2026-11-09): an iOS 27 iPhone is expected
    # over Rapport; its _airdrop._tcp result is dropped on purpose.
    for key, text in (("Dropping legacy endpoint", "sharingd: legacy _airdrop._tcp result dropped (expects Rapport)"),
                      ("Got DISCOVER response", "sharingd: DISCOVER answered"),
                      ("Adding discovered endpoint", "sharingd: endpoint added to the sheet"),
                      ("Received a new discovery result", "wifip2pd: discovery result"),
                      # Rapport, Mac as the sender (2026-11-11): PairVerify
                      # with the identities stored by the pairing by code.
                      ("Starting PairVerify with:", "rapportd: PairVerify with the peer started"),
                      ("Client PairVerify succeeded", "rapportd: PairVerify with the peer succeeded"),
                      ("Client PairVerify failed", "rapportd: PairVerify with the peer FAILED"),
                      ("Updated PIN info", "rapportd: AirDrop code generated on this Mac"),
                      ("Pairing SUCCESS", "rapportd: paired by code")):
        if key in line and seen.get(key, 0) < time.time() - 15:
            seen[key] = time.time()
            say(text)
    m = re.search(r"Updated browse client with (\d+) endpoints", line)
    if m and m.group(1) != "0" and seen.get("eps") != m.group(1):
        seen["eps"] = m.group(1)
        say("rapportd: %s endpoint(s) handed to AirDrop" % m.group(1))
' "$dir/system.log" & pids="$pids $!"

	finish() {
		trap - INT TERM HUP
		# shellcheck disable=SC2086
		kill $pids 2>/dev/null
		sleep 1
		awdl_ring > "$dir/ring_final.txt"
		ioreg -r -c RTL8814Native -d 1 > "$dir/layer_log_final.txt" 2>/dev/null
		snapshot end
		if [ -n "$udid" ]; then
			[ -n "$rvi" ] && rvictl -x "$udid" >> "$dir/rvictl.txt" 2>&1
			echo "Collecting the iPhone's log since $t0 (a minute or two; the iPhone stays on USB)"
			/usr/bin/log collect --device-udid "$udid" --start "$t0" --output "$dir/iphone.logarchive" \
				> "$dir/log_collect.txt" 2>&1 ||
				/usr/bin/log collect --device --start "$t0" --output "$dir/iphone.logarchive" \
					>> "$dir/log_collect.txt" 2>&1
			if [ -d "$dir/iphone.logarchive" ]; then
				/usr/bin/log show --archive "$dir/iphone.logarchive" --style compact --info --predicate \
					'process == "wifip2pd" OR process == "sharingd" OR process == "bluetoothd" OR process == "rapportd" OR process CONTAINS "AppleBCMWLAN" OR eventMessage CONTAINS[c] "awdl"' \
					> "$dir/iphone_system.log" 2>&1
			else
				inf "no iPhone log: $(tail -1 "$dir/log_collect.txt")"
			fi
		fi
		rm -f "$dir/.ring_seen" "$dir/.ring_now"
		chown -R "$owner" "$dir"
		echo
		cmd_report "$dir"
		echo
		echo "$dir"
		exit 0
	}
	trap finish INT TERM HUP

	echo "Collecting in $dir"
	echo "Ctrl-C ends it. HEARD <MAC> marks each new AWDL source."
	echo
	Tools/test_uc sim capture "$dir/frames.pcapng" 2>&1 | tee -i "$dir/capture.txt"
	finish
}

cmd_report() {
	local dir="${1:?run directory}" f
	[ -d "$dir" ] || { echo "no $dir"; exit 1; }
	echo "== Radio"
	/usr/bin/grep -E 'self-test|HEARD|radio released|^\[\+\] end|^\[-\]' "$dir/capture.txt" 2>/dev/null | cut -c1-140
	[ -f "$dir/frames.pcapng" ] && python3 -I "$ROOT/Tools/iphone_frames.py" "$dir/frames.pcapng"
	f="$dir/system.log"
	echo "== IO80211"
	inf "most peers in a state dump: $(/usr/bin/grep -o ' peers:[0-9]*' "$f" 2>/dev/null | cut -d: -f2 | sort -n | tail -1)"
	inf "cached: $(/usr/bin/grep -o 'cached:[0-9]*' "$f" 2>/dev/null | cut -d: -f2 | sort -n | tail -1)"
	inf "Peer Absence lines: $(/usr/bin/grep -c 'Peer Absence' "$f" 2>/dev/null)"
	# Data mode: the sequence widens with traffic (Docs/AWDL_MODO_DATOS.md).
	inf "passes to data mode: $(/usr/bin/grep -c 'reason:RptDataPathThres sc:Data' "$f" 2>/dev/null)"
	inf "radio: $(/usr/bin/grep -h 'awdl: tx psf' "$dir/ring_final.txt" 2>/dev/null | tail -1 | sed 's/.*data queued/data queued/')"
	/usr/bin/grep -E 'AWDL ON' "$f" 2>/dev/null | tail -1 | sed 's/.*awdl0: //' | cut -c1-140
	echo "== sharingd"
	inf "Found BLE device: $(/usr/bin/grep -c 'Found BLE device' "$f" 2>/dev/null)"
	/usr/bin/grep -E 'sharingd.*(AirDrop Bonjour server ready|Discover|/Ask|/Upload|Decompression|Import|Transfer|Failed|failed)' "$f" 2>/dev/null |
		tail -14 | cut -c1-170
	echo "== IP on awdl0"
	if [ -s "$dir/awdl0_ip.pcap" ]; then
		inf "packets: $(tcpdump -r "$dir/awdl0_ip.pcap" -n 2>/dev/null | wc -l | tr -d ' ')"
		inf "TCP to 8770: $(tcpdump -r "$dir/awdl0_ip.pcap" -n 'tcp port 8770' 2>/dev/null | wc -l | tr -d ' ')"
		tcpdump -r "$dir/awdl0_ip.pcap" -n 'ip6 and not icmp6' 2>/dev/null | awk '{print $3, ">", $5}' | sed 's/\.[0-9a-z]* >/ >/; s/:$//' | sort | uniq -c | sort -rn | head -8
	else
		inf "no capture"
	fi
	[ -f "$dir/iphone.txt" ] || return 0
	echo "== iPhone"
	inf "$(cut -f4,5 "$dir/iphone.txt" | tr '\t' ' '), pairing $(cut -f2 "$dir/iphone.txt")"
	if [ -s "$dir/iphone_ip.pcap" ]; then
		inf "IP packets on its interfaces: $(tcpdump -r "$dir/iphone_ip.pcap" -n 2>/dev/null | wc -l | tr -d ' ')"
		inf "of them on its awdl0: $(tcpdump -r "$dir/iphone_ip.pcap" -n -k I 2>/dev/null | /usr/bin/grep -c 'awdl0')"
		inf "TCP to 8770: $(tcpdump -r "$dir/iphone_ip.pcap" -n 'tcp port 8770' 2>/dev/null | wc -l | tr -d ' ')"
	else
		inf "no IP capture from the iPhone"
	fi
	f="$dir/iphone_system.log"
	[ -s "$f" ] || { inf "no iPhone log"; return 0; }
	inf "log lines: $(wc -l < "$f" | tr -d ' ')"
	inf "most peers in a state dump: $(/usr/bin/grep -o ' peers:[0-9]*' "$f" | cut -d: -f2 | sort -n | tail -1)"
	inf "Peer Absence lines: $(/usr/bin/grep -c 'Peer Absence' "$f")"
	local mac
	mac="$(awk '/^awdl0:/ {a = 1} a && /ether/ {print $2; exit}' "$dir/state_start.txt" 2>/dev/null)"
	if [ -n "$mac" ]; then
		# Whether the iPhone names this Mac at all, and what it says first and last
		inf "lines naming the Mac's awdl0: $(/usr/bin/grep -ic "$mac" "$f")"
		/usr/bin/grep -i "$mac" "$f" | sed -n '1p;$p' | cut -c1-170
	fi
	/usr/bin/grep -E 'sharingd.*(Discover|/Ask|/Upload|Transfer|Failed|failed)' "$f" | tail -8 | cut -c1-170
}

case "${1:-}" in
check)  cmd_check ;;
run)    cmd_run ;;
report) cmd_report "${2:-}" ;;
deploy) cmd_deploy ;;
channel) cmd_channel "${2:-}" ;;
*)      sed -n 3,13p "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
