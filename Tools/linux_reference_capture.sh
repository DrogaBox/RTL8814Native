#!/usr/bin/env bash
#
# RTL8814Native — Linux reference capture for the TP-Link Archer T9UH (2357:0106)
#
# Boot Linux, connect the T9UH to the SAME 5 GHz network yourself (NetworkManager
# GUI or nmcli; this script never asks for or reads the Wi-Fi password), then run:
#
#     sudo bash linux_reference_capture.sh
#
# Everything is written to linux_logs_<date>/ next to this script (works on the
# FAT32 pendrive). Bring that folder back to macOS.
#
# What it captures:
#   - kernel / driver / USB speed / module parameters
#   - full MAC/BB/RF register dumps from the driver's debugfs (rtw88) or
#     /proc/net/rtl8814au (Realtek out-of-tree driver), idle and after the tests
#   - iw link / station dump and mac80211 station counters before/after each test
#     (tx retries, tx failed, rx duplicates, A-MPDU)
#   - Cloudflare speed tests through the T9UH only: 3x 50 MB down, 3x 25 MB up
#   - tx/rx bitrate sampled every second during the upload
#
# Copyright (c) 2026 DrogaBox — GPL-2.0

set -u

if [ "$(id -u)" -ne 0 ]; then
	exec sudo bash "$0" "$@"
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/linux_logs_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT"
exec > >(tee "$OUT/run.log") 2>&1

say() { echo "[*] $*"; }
die() { echo "[!] $*"; exit 1; }

for tool in iw curl; do
	command -v "$tool" >/dev/null 2>&1 || die "Falta '$tool'. Instálalo (CachyOS: sudo pacman -S iw curl usbutils) y vuelve a correr el script."
done

# --- Find the T9UH network interface -------------------------------------------
IF=""
for n in /sys/class/net/*; do
	d="$(readlink -f "$n/device" 2>/dev/null)" || continue
	[ -f "$d/../idVendor" ] || continue
	if [ "$(cat "$d/../idVendor")" = "2357" ] && [ "$(cat "$d/../idProduct")" = "0106" ]; then
		IF="$(basename "$n")"
		USBDEV="$(readlink -f "$d/..")"
		break
	fi
done
[ -n "$IF" ] || die "No encontré la interfaz del T9UH (2357:0106). ¿Está conectado y cargó el driver? Revisa 'lsusb' y 'ip link'."

DRV="$(basename "$(readlink -f "/sys/class/net/$IF/device/driver")" 2>/dev/null)"
PHY="$(basename "$(readlink -f "/sys/class/net/$IF/phy80211")" 2>/dev/null)"
say "Interfaz: $IF  driver: $DRV  phy: $PHY  usb: $USBDEV"

if ! iw dev "$IF" link | grep -q "Connected to"; then
	die "$IF no está asociado. Conéctalo tú a la red de 5 GHz (NetworkManager / nmcli) y vuelve a correr el script."
fi

mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug 2>/dev/null

# Copy every readable file under $1 into $2 (write-only files are skipped).
dump_tree() {
	local src="$1" dst="$2" f rel
	[ -d "$src" ] || return 0
	find "$src" -maxdepth 5 -type f -perm -u=r 2>/dev/null | while read -r f; do
		rel="${f#"$src"/}"
		mkdir -p "$dst/$(dirname "$rel")"
		timeout 10 cat "$f" > "$dst/$rel" 2>&1
	done
}

RTW88_DIR="/sys/kernel/debug/ieee80211/$PHY/rtw88"
PROC_DIR=""
for p in /proc/net/rtl8814au /proc/net/rtl88xxau /proc/net/88XXau /proc/net/rtl8814a; do
	[ -d "$p/$IF" ] && PROC_DIR="$p/$IF" && break
done
MAC80211_STA_DIR="/sys/kernel/debug/ieee80211/$PHY/netdev:$IF/stations"

# --- System / driver info ------------------------------------------------------
say "Recolectando info del sistema"
{
	echo "== uname";          uname -a
	echo "== os-release";     cat /etc/os-release 2>/dev/null
	echo "== interface";      echo "IF=$IF DRV=$DRV PHY=$PHY USBDEV=$USBDEV"
	echo "== usb speed";      cat "$USBDEV/speed" 2>/dev/null; echo "(5000 = USB3 SuperSpeed, 480 = USB2 High-Speed)"
	echo "== lsusb -t";       lsusb -t 2>/dev/null
	echo "== modinfo";        modinfo "$DRV" 2>/dev/null
	echo "== module params"
	for m in "$DRV" rtw88_8814au rtw88_8814a rtw88_usb rtw88_core 8814au rtl8814au 88XXau; do
		[ -d "/sys/module/$m/parameters" ] || continue
		for p in /sys/module/$m/parameters/*; do echo "$m.$(basename "$p") = $(cat "$p" 2>/dev/null)"; done
	done
	echo "== ethtool -i";     ethtool -i "$IF" 2>/dev/null
	echo "== iw dev";         iw dev
	echo "== power save";     iw dev "$IF" get power_save 2>/dev/null
	echo "== ip addr";        ip addr show "$IF"
	echo "== ip route";       ip route
} > "$OUT/system.txt" 2>&1
iw phy "$PHY" info > "$OUT/iw_phy_info.txt" 2>&1
lsusb -v -d 2357:0106 > "$OUT/lsusb_v.txt" 2>&1

# --- Station / counter snapshot ----------------------------------------------
snap() {
	local tag="$1" d="$OUT/snap_$1"
	mkdir -p "$d"
	iw dev "$IF" link          > "$d/iw_link.txt" 2>&1
	iw dev "$IF" station dump  > "$d/iw_station_dump.txt" 2>&1
	for s in /sys/class/net/$IF/statistics/*; do echo "$(basename "$s") $(cat "$s")"; done > "$d/netdev_stats.txt"
	dump_tree "$MAC80211_STA_DIR" "$d/mac80211_stations"
	if [ -n "$PROC_DIR" ]; then
		for f in "$PROC_DIR"/*; do
			case "$(basename "$f")" in *reg_dump*|*efuse*) continue ;; esac
			[ -f "$f" ] && [ -r "$f" ] && timeout 10 cat "$f" > "$d/proc_$(basename "$f").txt" 2>&1
		done
	fi
	[ -r "$RTW88_DIR/phy_info" ] && timeout 10 cat "$RTW88_DIR/phy_info" > "$d/rtw88_phy_info.txt" 2>&1
}

# --- Register dumps (idle, connected) -----------------------------------------
say "Volcando registros (idle)"
dump_tree "$RTW88_DIR" "$OUT/regs_idle/rtw88"
[ -n "$PROC_DIR" ] && dump_tree "$PROC_DIR" "$OUT/regs_idle/proc"
dmesg > "$OUT/dmesg_before.txt" 2>&1

# --- Speed tests (bound to the T9UH only) -------------------------------------
UPFILE="$(mktemp /tmp/rtl8814_up.XXXXXX)"
head -c 25000000 /dev/urandom > "$UPFILE"
SUM="$OUT/summary.txt"
echo "IF=$IF DRV=$DRV usb_speed=$(cat "$USBDEV/speed" 2>/dev/null)" > "$SUM"
iw dev "$IF" link | grep -E "SSID|freq|signal|tx bitrate|rx bitrate" >> "$SUM"

snap 0_start

say "Bajada: 3 x 50 MB"
for i in 1 2 3; do
	curl --interface "$IF" -s -o /dev/null --max-time 90 \
		-w "DOWN run$i %{speed_download} B/s  %{time_total}s  http=%{http_code}\n" \
		"https://speed.cloudflare.com/__down?bytes=50000000" | tee -a "$SUM"
done
snap 1_after_down

say "Subida: 3 x 25 MB (muestreando la tasa cada segundo)"
( while :; do
	echo "--- $(date +%T.%N)"
	iw dev "$IF" station dump | grep -E "tx bitrate|rx bitrate|tx retries|tx failed|tx packets|rx packets|signal:"
	sleep 1
done ) > "$OUT/upload_rate_samples.txt" 2>&1 &
SAMPLER=$!
for i in 1 2 3; do
	curl --interface "$IF" -s -o /dev/null --max-time 90 -X POST --data-binary @"$UPFILE" \
		-w "UP   run$i %{speed_upload} B/s  %{time_total}s  http=%{http_code}\n" \
		"https://speed.cloudflare.com/__up" | tee -a "$SUM"
done
kill "$SAMPLER" 2>/dev/null
snap 2_after_up
rm -f "$UPFILE"

# --- Register dumps after the tests -------------------------------------------
say "Volcando registros (después de las pruebas)"
dump_tree "$RTW88_DIR" "$OUT/regs_after/rtw88"
[ -n "$PROC_DIR" ] && dump_tree "$PROC_DIR" "$OUT/regs_after/proc"
dmesg > "$OUT/dmesg_after.txt" 2>&1

awk '/DOWN|UP/ { printf "%s %s  %.0f Mbit/s\n", $1, $2, $3*8/1e6 }' "$SUM" | tee -a "$SUM.mbit"
sync
say "Listo. Resultados en: $OUT"
say "Apaga Linux, vuelve a macOS y avísame (el pendrive se monta en /Volumes)."
