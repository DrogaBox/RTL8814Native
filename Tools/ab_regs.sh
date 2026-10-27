#!/bin/bash
# Live A/B: poke one register group at a time, measure upload over en2, restore.
# Usage: Tools/ab_regs.sh  (link must be up; never handles credentials)
cd "$(dirname "$0")/.."
UC=./Tools/test_uc
UP=${UP_FILE:-/tmp/rtl8814_up.bin}
[ -s "$UP" ] || head -c 25000000 /dev/urandom > "$UP"
dup() { $UC status 2>/dev/null | sed -n 's/.*RX Reorder (A-MPDU) *: drop=\([0-9]*\).*/\1/p'; }
data() { $UC status 2>/dev/null | sed -n 's/.*ctrl=[0-9]*, data=\([0-9]*\).*/\1/p'; }
up() {
	local d0 r0 d1 r1
	d0=$(dup); r0=$(data)
	for i in 1 2; do
		curl --interface en2 -s -o /dev/null -X POST --data-binary @"$UP" --max-time 60 \
			-w "%{speed_upload}\n" https://speed.cloudflare.com/__up
	done | awk '{s=s sprintf(" %.0f", $1*8/1e6)} END {printf "up Mbit/s:%s", s}'
	d1=$(dup); r1=$(data)
	echo "   rx_dup=$((d1-d0))/$((r1-r0))"
}
peek() { $UC peek "$1" "$2" | awk '{print $3}'; }
poke() { $UC poke "$1" "$2" "$3" >/dev/null; }
# name addr width test_value
TESTS=(
	"RRSR_RSC 0x442 1 0x00"
	"R2T_SIFS_OFDM 0x63F 1 0x0E"
	"TX_SIFS_OFDM 0x514 4 0x100A100A"
	"PKT_LIFE_VO_VI 0x4C0 4 0x10001000"
)
echo "baseline"; up
for t in "${TESTS[@]}"; do
	set -- $t
	old=$(peek "$2" "$3")
	poke "$2" "$4" "$3"
	echo "$1 ($2: $old -> $(peek "$2" "$3"))"; up
	poke "$2" "$old" "$3"
done
echo "baseline again"; up
