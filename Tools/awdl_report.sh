#!/bin/bash
#
# Report of an AirDrop test over AWDL, safe to attach to a public issue.
#
#   Tools/awdl_report.sh check          is this Mac ready for the test; no root, changes nothing
#   sudo Tools/awdl_report.sh run       collect during the AirDrop test until Ctrl-C, then ask a few
#                                       questions and write the report (same as pack)
#   Tools/awdl_report.sh pack <run dir> [--answers <file>]
#                                       write <run dir>/awdl_report.txt from an existing run folder
#   Tools/awdl_report.sh verify <file>  repeat the redaction self-check on a report edited by hand
#
# The run folder (~/Desktop/iphone_<date>/, made by Tools/iphone_test.sh run) holds raw captures and
# logs with names and addresses: it stays private. Only awdl_report.txt is meant to be posted, in a
# new issue at https://github.com/DrogaBox/RTL8814Native/issues
#
# Answers of the questionnaire, for a scripted pack: one key=value per line in the --answers file,
# or AWDL_REPORT_<KEY> in the environment (AWDL_REPORT_DEVICE_MODEL, ...). The file wins, then the
# environment, then <run dir>/answers.txt from an earlier pack. Keys:
#   device_model, device_os, direction (device-to-mac | mac-to-device | both),
#   mac_in_device_sheet, device_in_mac_sheet, transfer_started, transfer_finished (yes | no),
#   file_size, seconds, wifi_associated (yes | no), wifi_band (2.4 | 5 | 6), notes
#
set -u
START_DIR="$PWD"
cd "$(dirname "$0")/.." || exit 1
ROOT="$PWD"

VERSION=1
ISSUES_URL=https://github.com/DrogaBox/RTL8814Native/issues
DRIVER_ID=com.DrogaBox.driver.RTL8814Native
LAYER_ID=com.DrogaBox.driver.RTL8814AirLayer
# The layer sets it when it creates awdl0 (see Tools/iphone_test.sh).
AWDL_MTU=1428
ANSWER_KEYS="device_model device_os direction mac_in_device_sheet device_in_mac_sheet transfer_started transfer_finished file_size seconds wifi_associated wifi_band notes"
TEST_UC_BUILD="clang -Wall -O2 Tools/test_uc.c -framework IOKit -framework CoreFoundation -o Tools/test_uc"
PASSED=1
WORK=""

ok()   { printf 'ok       %s\n' "$*"; }
bad()  { printf 'MISSING  %s\n' "$*"; PASSED=0; }
note() { printf '         %s\n' "$*"; }

usage() { sed -n 3,22p "$0" | sed 's/^# \{0,1\}//'; }

abs_path() { case "$1" in /*) printf '%s\n' "$1" ;; *) printf '%s\n' "$START_DIR/$1" ;; esac; }

owner_name() {
	if [ "$(id -u)" = 0 ] && [ -n "${SUDO_USER:-}" ]; then printf '%s\n' "$SUDO_USER"; else id -un; fi
}

as_owner() {
	if [ "$(id -u)" = 0 ] && [ -n "${SUDO_USER:-}" ]; then sudo -u "$SUDO_USER" "$@"; else "$@"; fi
}

# /usr/bin/python3 is a stub that opens an install dialog when the Command Line Tools are missing.
have_python() {
	local p
	p="$(command -v python3)" || return 1
	[ "$p" != /usr/bin/python3 ] || xcode-select -p > /dev/null 2>&1
}

need_python() {
	have_python && return 0
	echo "python3 is needed (it comes with the Command Line Tools: xcode-select --install)"
	exit 1
}

kext_info() {   # bundle id -> "version UUID", empty when not loaded
	kextstat -b "$1" 2>/dev/null | awk -v b="$1" '$6 == b {v = $7; gsub(/[()]/, "", v); print v, $8; exit}'
}

awdl_mtu() { ifconfig awdl0 2>/dev/null | awk 'NR == 1 {print $NF}'; }

# Only the kexts' own boot-args: the rest of the line says too much about the machine.
rtl_boot_args() {
	nvram boot-args 2>/dev/null | cut -f2- | tr ' \t' '\n\n' |
		/usr/bin/grep -E '^-?rtl(air|awdl)[A-Za-z0-9_]*(=[A-Za-z0-9._,:-]*)?$' | tr '\n' ' ' | sed 's/ $//'
}

field() { printf '%s\n' "$1" | sed -n "s/^$2=//p" | head -1; }

# Redaction, report and self-check. One program, several subcommands.
IFS= read -r -d '' PY_PROG <<'PYEOF'
import collections
import os
import plistlib
import re
import sys
import time

ISSUES = "https://github.com/DrogaBox/RTL8814Native/issues"
RING_CAP = 120000

# Addresses that identify nobody and stay as they are.
KEEP_MACS = ("ff:ff:ff:ff:ff:ff", "00:25:00:ff:94:73", "00:00:00:00:00:00")
KEEP_PREFIXES = ("33:33:", "01:00:5e:")
# Source address of the frame Tools/test_uc injects to test the capture.
SELFTEST_MAC = "02:00:00:aa:bb:fe"

MAC_COLON = re.compile(r"(?<![0-9A-Fa-f:])[0-9A-Fa-f]{1,2}(?::[0-9A-Fa-f]{1,2}){5}(?![0-9A-Fa-f:])")
MAC_DASH = re.compile(r"(?<![0-9A-Fa-f-])[0-9A-Fa-f]{2}(?:-[0-9A-Fa-f]{2}){5}(?![0-9A-Fa-f-])")
LL_RE = re.compile(r"(?<![0-9A-Fa-f:])fe80:[0-9A-Fa-f:]*[0-9A-Fa-f]", re.I)
# A dump of frame bytes (the layer logs its sync template once): addresses and names inside it
HEXDUMP_RE = re.compile(r"(?<![0-9A-Fa-f])[0-9A-Fa-f]{20,}(?![0-9A-Fa-f])")
HOME_RE = re.compile(r"/(?:Users|home)/[^/\s\"'<>]+")
EMAIL_RE = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+")
PHONE_RE = re.compile(r"(?<![\w.:])\+?\d(?:[ ().-]{0,2}\d){8,}(?![\w:])")
IPV4_RE = re.compile(r"(?<![\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?![\d.])")
BOOTARG_RE = re.compile(r"-?rtl(?:air|awdl)[A-Za-z0-9_]*(?:=[A-Za-z0-9._,:-]*)?")
SERVICE_RE = re.compile(r"_[a-z0-9-]{2,30}\._(?:tcp|udp)")
AWDL_ON_RE = re.compile(r"AWDL ON:\[infra\(\d+\) \d+%\] \[sdb:\d+\] \(\d+/\d+/\d+\) \[[\d ]+\](?: [A-Za-z]+){0,4}")
LOG_RE = re.compile(r"^(\d{4}-\d\d-\d\d) (\d\d:\d\d:\d\d)\.\d+\S*\s+\S+\s+([A-Za-z0-9_.]+)\[")

# Words that are no one's name: a computer called "MacBook Pro" is not private, and
# redacting "mac" or "radio" would empty the report.
GENERIC = set("""
mac macbook macbookpro macbookair imac macmini macpro macstudio mini pro air studio book apple
hackintosh ryzentosh hack computer desktop laptop home office work user users admin administrator
root guest test tester local lan wifi airport the and von van der del los las iphone ipad ipod
watch airpods airpod homepod max plus ultra magic mouse keyboard trackpad pencil beats speaker
headphones headset buds phone tablet radio driver device peer self network data log report run
bluetooth usb realtek tplink archer adapter dongle nic wlan ryzen amd intel core xeon processor
threadripper epyc none unknown yes not for with from host name email address addr ipv4 ipv6
selftest hex bytes left out
""".split())

# Known lines of the system log, counted and never copied: (process, text searched, meaning).
EVENTS = [
    ("kernel", "AWDL ON", "IO80211: AWDL state line"),
    ("kernel", " peers:", "IO80211: state dump with the peer count"),
    ("kernel", "Posted Peer Presence", "IO80211: peer presence posted to the stack"),
    ("kernel", "Peer Absence", "IO80211: peer absence"),
    ("kernel", "shouldRemovePeerFromNDP", "IO80211: peer silent, checked for removal"),
    ("kernel", "reason:RptDataPathThres sc:Data", "IO80211: schedule to data mode by traffic"),
    ("kernel", "SYNC_INFO", "IO80211: peer frame outside the sync window"),
    ("kernel", "BAD OUI", "IO80211: action frame with a bad OUI"),
    ("kernel", "Invalid frame", "IO80211: invalid frame"),
    ("kernel", "Invalid _htCapabilityLength", "IO80211: HT capabilities missing from the templates"),
    ("kernel", "enable state mismatch", "IO80211: sync enable state mismatch"),
    ("bluetoothd", "STATUS 712", "bluetoothd: BLE scan error 712"),
    ("sharingd", "Found BLE device", "sharingd: BLE device found"),
    ("sharingd", "Lost BLE device", "sharingd: BLE device lost"),
    ("sharingd", "Device in everyone mode", "sharingd: receiving from everyone"),
    ("sharingd", "AirDrop Bonjour server ready", "sharingd: AirDrop server ready"),
    ("sharingd", "AirDrop connection ready", "sharingd: incoming AirDrop connection ready"),
    ("sharingd", "AirDrop connection failed", "sharingd: AirDrop connection failed"),
    ("sharingd", "Received ASK request", "sharingd: ASK received (the device asks to send)"),
    ("sharingd", "Received UPLOAD request", "sharingd: UPLOAD received (file data starts)"),
    ("sharingd", "SDAirDropReceiveService.transfers ADD", "sharingd: incoming transfer created"),
    ("sharingd", "Decompression progress", "sharingd: incoming data progress"),
    ("sharingd", "Saving transfer to Downloads folder", "sharingd: incoming transfer saved"),
    ("sharingd", "Error receiving message", "sharingd: error receiving a message"),
    ("sharingd", "Endpoints discovered", "sharingd: browse returned endpoints"),
    ("sharingd", "Dropping legacy endpoint", "sharingd: legacy _airdrop._tcp result dropped (expects Rapport)"),
    ("sharingd", "Sending DISCOVER request", "sharingd: DISCOVER sent"),
    ("sharingd", "Got DISCOVER response", "sharingd: DISCOVER answered"),
    ("sharingd", "Adding discovered endpoint", "sharingd: endpoint added to the sheet"),
    ("sharingd", "No PIN info found in TXT record", "sharingd: TXT record without PIN info"),
    ("wifip2pd", "Created AWDLBrowse", "wifip2pd: AWDL browse created"),
    ("wifip2pd", "Received a new discovery result", "wifip2pd: discovery result"),
    ("wifip2pd", "Posted peer presence", "wifip2pd: peer presence posted (neighbour entry)"),
    ("wifip2pd", "Failed to query the TXT record", "wifip2pd: TXT query failed"),
    ("rapportd", "Starting PairVerify with:", "rapportd: PairVerify with the peer started"),
    ("rapportd", "Client PairVerify succeeded", "rapportd: PairVerify with the peer succeeded"),
    ("rapportd", "Client PairVerify failed", "rapportd: PairVerify with the peer FAILED"),
    ("rapportd", "Server PairVerify succeeded", "rapportd: PairVerify from the peer succeeded"),
    ("rapportd", "PairVerify timeout", "rapportd: PairVerify timeout"),
    ("rapportd", "Updated PIN info", "rapportd: AirDrop code generated on this Mac"),
    ("rapportd", "Pairing SUCCESS", "rapportd: paired by code"),
    ("rapportd", "Attempting discovery with", "rapportd: discovery attempt"),
    ("rapportd", "Skipping PairVerify for our own", "rapportd: own Bonjour device skipped"),
    ("rapportd", "Updated browse client with", "rapportd: endpoints handed to AirDrop"),
]
LOG_PROCS = ("kernel", "sharingd", "wifip2pd", "rapportd", "bluetoothd", "mDNSResponder")
SERVICE_PROCS = ("wifip2pd", "mDNSResponder", "sharingd", "rapportd")

QUESTIONS = [
    ("device_model", "Apple device model"),
    ("device_os", "its OS version"),
    ("direction", "direction tested"),
    ("mac_in_device_sheet", "the Mac appeared in the device's AirDrop sheet"),
    ("device_in_mac_sheet", "the device appeared in the Mac's AirDrop sheet"),
    ("transfer_started", "a transfer started"),
    ("transfer_finished", "it finished"),
    ("file_size", "file size"),
    ("seconds", "seconds"),
    ("wifi_associated", "Mac's Wi-Fi associated to a network during the test"),
    ("wifi_band", "band of that network"),
    ("notes", "notes"),
]


def read_lines(path):
    try:
        with open(path, errors="replace") as f:
            return f.read().splitlines()
    except OSError:
        return []


def norm_mac(s):
    s = s.strip().lower()
    if re.fullmatch(r"[0-9a-f]{12}", s):
        return ":".join(s[i:i + 2] for i in range(0, 12, 2))
    parts = re.split(r"[:-]", s)
    if len(parts) != 6:
        return None
    try:
        return ":".join("%02x" % int(p, 16) for p in parts)
    except ValueError:
        return None


def mac_kept(m):
    return m in KEEP_MACS or m.startswith(KEEP_PREFIXES)


def ipv4_public(m):
    """A dotted quad that is no host address: multicast, broadcast, all zeros, or no address at all."""
    octets = [int(g) for g in m.groups()]
    return max(octets) > 255 or octets[0] >= 224 or not any(octets)
def ll_interface_id(addr):
    """The last four groups of a link-local address, None when it does not parse."""
    head, sep, tail = addr.lower().partition("::")
    h = head.split(":") if head else []
    t = tail.split(":") if tail else []
    groups = h + ["0"] * (8 - len(h) - len(t)) + t if sep else h
    if len(groups) != 8:
        return None
    try:
        return [int(g, 16) for g in groups][4:]
    except ValueError:
        return None


class Identity:
    """What must not reach the report: own addresses, names, serials."""

    def __init__(self):
        self.macs = []       # this Mac's own, in pseudonym order
        self.names = []      # (kind, value)
        self.known = set()   # every address the raw files hold

    def add_mac(self, s, front=False):
        m = norm_mac(s)
        if not m or mac_kept(m) or m == SELFTEST_MAC:
            return
        self.known.add(m)
        if m in self.macs:
            return
        if front:
            self.macs.insert(0, m)
        else:
            self.macs.append(m)

    def add_name(self, kind, value):
        value = value.strip()
        if len(value) >= 2 and (kind, value) not in self.names:
            self.names.append((kind, value))


def parse_bluetooth(lines, ident):
    """system_profiler SPBluetoothDataType: a name line followed by its Address line."""
    prev = None
    for l in lines:
        m = re.match(r"\s+Address: (\S+)\s*$", l)
        if m and prev is not None:
            if prev == "Bluetooth Controller":
                ident.add_mac(m.group(1))
            else:
                ident.add_name("btname", prev)
                n = norm_mac(m.group(1))
                if n:
                    ident.known.add(n)
        m = re.match(r"\s+(.+):\s*$", l)
        prev = m.group(1) if m else None


def load_identity(run, work):
    ident = Identity()

    def identity_file(path, own_run):
        for line in read_lines(path):
            f = line.split("\t")
            if len(f) < 2 or not f[1].strip():
                continue
            if f[0] == "mac":
                ident.add_mac(f[1], front=own_run and len(f) > 2 and f[2] == "awdl0")
            else:
                ident.add_name(f[0], f[1])

    identity_file(os.path.join(run, "private_identity.txt"), True)
    for name in ("state_start.txt", "state_end.txt"):
        lines = read_lines(os.path.join(run, name))
        for l in lines:
            m = re.match(r"\s*ether (\S+)", l)
            if m:
                ident.add_mac(m.group(1))
        parse_bluetooth(lines, ident)
    for name in ("uc_status_start.txt", "uc_status_end.txt"):
        for l in read_lines(os.path.join(run, name)):
            m = re.match(r"\[\+\] Permanent Hardware MAC\s*: (\S+)", l)
            if m:
                ident.add_mac(m.group(1))
            m = re.match(r"\[\+\] Connected SSID\s*: (.*)$", l)
            if m:
                ident.add_name("network", m.group(1))
    for name in ("capture.txt", "README.txt"):
        for l in read_lines(os.path.join(run, name))[:5]:
            for m in HOME_RE.finditer(l):
                ident.add_name("user", m.group(0).split("/")[2])
    for l in read_lines(os.path.join(run, "iphone.txt")):
        f = l.split("\t")
        if len(f) >= 3:
            ident.add_name("udid", f[0])
            ident.add_name("device", f[2])
    identity_file(os.path.join(work, "identity_live.txt"), False)
    parse_bluetooth(read_lines(os.path.join(work, "bt_live.txt")), ident)
    for name in ("capture.txt", "ring.txt", "ring_final.txt", "state_start.txt", "state_end.txt",
                 "uc_status_start.txt", "uc_status_end.txt"):
        for l in read_lines(os.path.join(run, name)):
            for rx in (MAC_COLON, MAC_DASH):
                for m in rx.finditer(l):
                    n = norm_mac(m.group(0))
                    if n and not mac_kept(n) and n != SELFTEST_MAC:
                        ident.known.add(n)
    return ident


def private_strings(ident):
    """(kind, string) pairs to remove: whole names, and the words of personal ones."""
    out = []
    seen = set()

    def add(kind, s):
        key = s.lower()
        if key not in seen:
            seen.add(key)
            out.append((kind, s))

    for kind, value in ident.names:
        if kind == "home":
            continue
        if kind in ("serial", "uuid", "usbserial", "udid"):
            if len(value) >= 4:
                add("id", value)
            continue
        words = [w for w in re.split(r"[^0-9A-Za-z\u00c0-\uffff]+", value) if w]
        personal = [w for w in words if len(w) >= 3 and w.lower() not in GENERIC and not w.isdigit()]
        if not personal:
            continue   # a generic name: nothing private in it
        add(kind, value)
        if kind in ("computer", "host", "fullname", "device", "btname"):
            for w in personal:
                add("word", w)
    out.sort(key=lambda kv: -len(kv[1]))
    return out


NAME_LABEL = {
    "user": "<user>", "host": "<host>", "computer": "<host>", "fullname": "<name>", "word": "<name>",
    "network": "<network>", "device": "<device-name>", "btname": "<device-name>", "id": "<id>",
}


def name_regex(s):
    parts = []
    for c in s:
        if c in "'\u2019":
            parts.append("['\u2019]")
        elif c in " _-":
            parts.append("[ _-]")
        else:
            parts.append(re.escape(c))
    return re.compile(r"(?<![0-9A-Za-z])" + "".join(parts) + r"(?![0-9A-Za-z])", re.I)


class Redactor:
    def __init__(self, ident):
        self.selfs = {}
        for m in ident.macs:
            self.selfs[m] = "self-%d" % (len(self.selfs) + 1)
        self.peers = {}
        self.others = {}
        self.known = set(ident.known)
        self.names = [(name_regex(s), NAME_LABEL[k]) for k, s in private_strings(ident)]
        self.count = collections.Counter()

    def mac(self, s):
        m = norm_mac(s)
        if m is None:
            return s
        if mac_kept(m):
            return m
        if m == SELFTEST_MAC:
            return "selftest-address"
        if m in self.selfs:
            return self.selfs[m]
        return self.peers.setdefault(m, "peer-%d" % (len(self.peers) + 1))

    def other(self, kind, a):
        key = (kind, a.lower())
        if key not in self.others:
            n = 1 + sum(1 for k in self.others if k[0] == kind)
            self.others[key] = "<%s-%d>" % (kind, n)
        return self.others[key]

    def link_local(self, a):
        iid = ll_interface_id(a)
        if iid and (iid[1] & 0xFF) == 0xFF and (iid[2] >> 8) == 0xFE:
            m = "%02x:%02x:%02x:%02x:%02x:%02x" % ((iid[0] >> 8) ^ 2, iid[0] & 0xFF, iid[1] >> 8,
                                                 iid[2] & 0xFF, iid[3] >> 8, iid[3] & 0xFF)
            return "fe80::<%s>" % self.mac(m)
        return "fe80::" + self.other("addr", a)

    def ip(self, a):
        """An address as tcpdump prints it, without the port."""
        low = a.lower()
        if low.startswith("fe80:"):
            return self.link_local(low)
        if ":" in low:
            return low if low.startswith("ff") else self.other("ipv6", low)
        m = IPV4_RE.fullmatch(low)
        if m:
            return low if int(m.group(1)) >= 224 or low == "0.0.0.0" else self.other("ipv4", low)
        return "<address>"

    def text(self, s):
        """A line of this project's own tools: its addresses become pseudonyms."""
        s = LL_RE.sub(lambda m: self.link_local(m.group(0)), s)
        s = MAC_COLON.sub(lambda m: self.mac(m.group(0)), s)
        s = MAC_DASH.sub(lambda m: self.mac(m.group(0)), s)
        s, n = HEXDUMP_RE.subn(lambda m: "<%d bytes of hex left out>" % (len(m.group(0)) // 2), s)
        self.count["hex dumps left out"] += n
        return s

    def final(self, s):
        """Last pass over the whole report: paths, e-mail addresses, names, bare addresses."""
        s, n = HOME_RE.subn("<home>", s)
        self.count["home paths replaced"] += n
        s, n = EMAIL_RE.subn("<email>", s)
        self.count["e-mail addresses replaced"] += n
        s = IPV4_RE.sub(lambda m: m.group(0) if ipv4_public(m) else "<ipv4>", s)
        for rx, label in self.names:
            s, n = rx.subn(label, s)
            self.count["names replaced"] += n
        # Known addresses written without separators, also inside a longer hex string.
        for m in sorted(self.known | set(self.selfs) | set(self.peers)):
            s = re.sub(m.replace(":", ""), lambda _m, m=m: self.mac(m), s, flags=re.I)
        return s


def clean(s, limit):
    s = "".join(c if c.isprintable() else " " for c in s)
    return re.sub(r"\s+", " ", s).strip()[:limit]


def load_kv(path):
    d = {}
    for l in read_lines(path):
        if l.startswith("#") or "=" not in l:
            continue
        k, v = l.split("=", 1)
        k = k.strip()
        if k == "kext":
            d.setdefault("kext", []).append(v.strip())
        else:
            d[k] = v.strip()
    return d


def derive_sysinfo(run):
    """What a folder made by Tools/iphone_test.sh run alone says about the system."""
    d = {}
    st = read_lines(os.path.join(run, "state_start.txt"))
    if len(st) >= 2 and re.fullmatch(r"\d\d[A-Z]\d+[a-z]?", st[1].strip()):
        d["macos_build"] = st[1].strip()
    for l in st:
        if l.startswith("boot-args"):
            d["boot_args"] = " ".join(t for t in l.split()[1:] if BOOTARG_RE.fullmatch(t))
        m = re.search(r"com\.DrogaBox\.driver\.(\w+) \(([\w.]+)\) ([0-9A-Fa-f-]{36})", l)
        if m:
            d.setdefault("kext", []).append("%s %s %s" % m.groups())
        m = re.match(r"awdl0: .* mtu (\d+)", l)
        if m:
            d["awdl_mtu"] = m.group(1)
    text = "\n".join(read_lines(os.path.join(run, "layer_log_final.txt")))
    m = re.search(r'"RTLAirGate" = "([A-Za-z0-9 ,.:_-]{1,200})"', text)
    if m:
        d["gate"] = m.group(1)
    v = re.search(r'"idVendor" = (\d+)', text)
    p = re.search(r'"idProduct" = (\d+)', text)
    if v and p:
        d["usb_id"] = "%04x:%04x" % (int(v.group(1)), int(p.group(1)))
    return d


def uc_status(path, red):
    """Tools/test_uc status, with the network name left out."""
    out = []
    for l in read_lines(path):
        if re.match(r"\[\+\] Connected SSID\s*:", l):
            out.append(l.split(":", 1)[0] + ": <network>")
        elif re.match(r"\[[+-]\] |=+|\s+bad_pktlen=", l):
            out.append(red.text(l.rstrip()))
    return out


def ring_lines(run):
    """(time, text) of the layer's ring as this run saw it, oldest first."""
    out = []
    seen = set()
    for l in read_lines(os.path.join(run, "ring.txt")):
        m = re.match(r"(\d\d:\d\d:\d\d) (.*)$", l)
        if m:
            out.append((m.group(1), m.group(2).rstrip()))
            seen.add(m.group(2).rstrip())
    for l in read_lines(os.path.join(run, "ring_final.txt")):
        l = l.rstrip()
        if l and l not in seen:
            out.append(("  end   ", l))
    return out


def block_ack(ring, red, out):
    amp = [(t, x) for t, x in ring if x.startswith("awdl: A-MPDU frames ")]
    req = [x for t, x in ring if x.startswith("awdl: ADDBA request to ")]
    rsp = [x for t, x in ring if x.startswith("awdl: ADDBA response from ")]
    dead = [x for t, x in ring if x.startswith("awdl: Block Ack session to ")]
    out.append("As originator (the Mac asks the device to take aggregated frames):")
    nums = None
    for tag, item in (("first", amp[:1]), ("last", amp[-1:] if len(amp) > 1 else [])):
        for t, x in item:
            out.append("  %-5s %s %s" % (tag, t, red.text(x)[:400]))
    if amp:
        m = re.search(r"sessions asked (\d+) on (\d+) refused (\d+) unanswered (\d+) closed by peer (\d+) "
                      r"broken (\d+)", amp[-1][1])
        if m:
            nums = [int(g) for g in m.groups()]
    out.append("  ADDBA requests logged: %d, responses logged: %d, sessions closed for silence: %d"
               % (len(req), len(rsp), len(dead)))
    for x in rsp[:8]:
        out.append("  " + red.text(x)[:300])
    status = [int(m.group(1)) for m in (re.search(r" status (\d+) ", x) for x in rsp) if m]
    good = sum(1 for s in status if s == 0)
    if nums and nums[0]:
        asked, on, refused, unanswered = nums[:4]
        if on:
            verdict = "ACCEPTED: %d of %d requests answered with success (counters since boot)" % (on, asked)
        elif refused:
            verdict = "REFUSED: %d of %d requests refused, none accepted" % (refused, asked)
        else:
            verdict = "NO ANSWER: %d requests, %d unanswered, none accepted" % (asked, unanswered)
    elif status:
        verdict = ("ACCEPTED: %d of %d logged responses carry status 0" % (good, len(status)) if good
                   else "REFUSED: %d logged responses, none with status 0" % len(status))
    elif nums:
        verdict = "the Mac asked for no session (no aggregated data to send, or aggregation off)"
    elif req:
        verdict = "NO ANSWER: %d requests logged and no response" % len(req)
    else:
        verdict = ("not known: no originator line in the ring (the layer logs it about every two "
                   "minutes of AWDL, and only with aggregation on)")
    out.append("  did the device accept the aggregation request: " + verdict)
    out.append("As recipient (the device asks the Mac):")
    ba = [(t, x) for t, x in ring if x.startswith("awdl: ba req ")]
    got = [x for t, x in ring if x.startswith("awdl: ADDBA from ")]
    out.append("  ADDBA requests from peers logged: %d" % len(got))
    for x in got[:4]:
        out.append("  " + red.text(x)[:300])
    if ba:
        out.append("  last %s %s" % (ba[-1][0], red.text(ba[-1][1])[:300]))
    else:
        out.append("  no 'awdl: ba req' counter line in the ring")


def scan_log(path, red):
    """Counts of known events in a unified log file written with --style compact."""
    by_proc = collections.defaultdict(list)
    for p, needle, label in EVENTS:
        by_proc[p].append(needle)
    ev = {}
    procs = collections.Counter()
    services = collections.Counter()
    r = {"lines": 0, "first": None, "last": None, "peers": None, "cached": None, "endpoints": None,
         "awdl_on": None}
    try:
        f = open(path, errors="replace")
    except OSError:
        return None
    with f:
        for line in f:
            if not line[:4].isdigit():
                continue
            m = LOG_RE.match(line)
            if not m:
                continue
            ts = (m.group(1), m.group(2))
            proc = m.group(3)
            r["lines"] += 1
            if r["first"] is None:
                r["first"] = ts
            r["last"] = ts
            procs[proc] += 1
            for needle in by_proc.get(proc, ()):
                if needle in line:
                    e = ev.setdefault((proc, needle), [0, ts, ts])
                    e[0] += 1
                    e[2] = ts
            if proc == "kernel":
                if " peers:" in line:
                    m = re.search(r" peers:(\d+), cached:(\d+)", line)
                    if m:
                        r["peers"] = max(r["peers"] or 0, int(m.group(1)))
                        r["cached"] = max(r["cached"] or 0, int(m.group(2)))
                if "AWDL ON:" in line:
                    m = AWDL_ON_RE.search(line)
                    if m:
                        r["awdl_on"] = m.group(0)
            elif proc == "rapportd" and "Updated browse client with" in line:
                m = re.search(r"Updated browse client with (\d+) endpoints", line)
                if m:
                    r["endpoints"] = max(r["endpoints"] or 0, int(m.group(1)))
            if proc in SERVICE_PROCS and "._" in line:
                for s in SERVICE_RE.findall(line):
                    services[(proc, s)] += 1
    r["events"] = ev
    r["procs"] = procs
    r["services"] = services
    return r


def log_section(r, out):
    if r["first"]:
        out.append("log lines read: %d, from %s %s to %s %s" % (r["lines"], r["first"][0], r["first"][1],
                                                               r["last"][0], r["last"][1]))
    out.append("lines by process: " + ", ".join("%s %d" % (p, r["procs"][p]) for p in LOG_PROCS))
    out.append("")
    out.append("%7s  %-8s  %-8s  %s" % ("count", "first", "last", "event"))
    for p, needle, label in EVENTS:
        e = r["events"].get((p, needle))
        if e:
            out.append("%7d  %-8s  %-8s  %s" % (e[0], e[1][1], e[2][1], label))
        else:
            out.append("%7d  %-8s  %-8s  %s" % (0, "-", "-", label))
    out.append("")
    out.append("most peers in an IO80211 state dump: %s, most cached: %s"
               % ("-" if r["peers"] is None else r["peers"], "-" if r["cached"] is None else r["cached"]))
    out.append("most endpoints rapportd handed to AirDrop: %s" % ("-" if r["endpoints"] is None else r["endpoints"]))
    if r["awdl_on"]:
        out.append("last AWDL state line: " + r["awdl_on"])
    if r["services"]:
        out.append("")
        out.append("Bonjour service types named in the log (type and count, never instance names):")
        for (p, s), n in sorted(r["services"].items(), key=lambda kv: (kv[0][0], -kv[1], kv[0][1])):
            out.append("%7d  %-13s %s" % (n, p, s))


def split_port(a):
    """address.port as tcpdump -n prints it -> (address, port)."""
    if ":" in a or a.count(".") == 4:
        head, dot, port = a.rpartition(".")
        if dot and port.isdigit():
            return head, port
    return a, ""


def ip_section(path, red, out):
    """IP on awdl0 from tcpdump -n -tt: flows and what mDNS carries, addresses replaced."""
    flows = {}
    mdns = collections.Counter()
    services = collections.Counter()
    flags = collections.Counter()
    total = 0
    try:
        f = open(path, errors="replace")
    except OSError:
        out.append("no IP capture on awdl0 in this run")
        return
    with f:
        for line in f:
            m = re.match(r"(\d+)\.\d+ (IP6?) (\S+) > (\S+): (.*)$", line.rstrip())
            if not m:
                continue
            total += 1
            t = int(m.group(1))
            src, sport = split_port(m.group(3))
            dst, dport = split_port(m.group(4))
            rest = m.group(5)
            src = red.ip(src)
            dst = red.ip(dst)
            if "5353" in (sport, dport):
                proto = "mDNS"
                m = re.search(r"\((\d+)\)\s*$", rest)
                size = int(m.group(1)) if m else 0
                for q in re.findall(r'"flags=(\d{1,12})"', rest):
                    flags[(src, q)] += 1
                bare = re.sub(r'"[^"]*"', "", rest)
                for s in SERVICE_RE.findall(bare):
                    services[(src, s)] += 1
                for kind, asked in re.findall(r"(?<![\w.-])(PTR|SRV|TXT|AAAA|A|ANY|NSEC|OPT)(?![\w.-])( \(Q[MU]\)\?)?", bare):
                    mdns[(src, "question" if asked else "record", kind)] += 1
            else:
                proto = ("TCP" if rest.startswith("Flags [") else "UDP" if rest.startswith("UDP,")
                         else "ICMP6" if "ICMP6" in rest else "other")
                m = re.search(r"length (\d+)\s*$", rest)
                size = int(m.group(1)) if m else 0
                if "8770" in (sport, dport) and proto == "TCP":
                    proto = "TCP port 8770"
            e = flows.setdefault((proto, src, dst), [0, 0, t, t, collections.Counter()])
            e[0] += 1
            e[1] += size
            e[3] = t
            e[4][t // 10] += size
    out.append("packets: %d" % total)
    out.append("")
    out.append("%-13s %8s %11s  %-8s  %-8s  %6s  %6s  %s" % ("protocol", "packets", "bytes", "first", "last",
                                                               "kB/s", "peak", "source > destination"))
    rows = sorted(flows.items(), key=lambda kv: -kv[1][1])
    for (proto, src, dst), (n, size, t0, t1, buckets) in rows[:14]:
        big = t1 - t0 >= 5 and size >= 100000
        rate = "%.0f" % (size / 1000.0 / (t1 - t0)) if big else "-"
        peak = "%.0f" % (max(buckets.values()) / 10000.0) if big else "-"
        out.append("%-13s %8d %11d  %-8s  %-8s  %6s  %6s  %s > %s" % (
            proto, n, size, time.strftime("%H:%M:%S", time.localtime(t0)),
            time.strftime("%H:%M:%S", time.localtime(t1)), rate, peak, src, dst))
    if len(rows) > 14:
        out.append("(%d smaller flows not listed)" % (len(rows) - 14))
    out.append("bytes are payload bytes; kB/s is the average between a flow's first and last packet, "
               "peak its best 10 s")
    if mdns:
        out.append("")
        out.append("mDNS on awdl0 (record kinds by sender, never names):")
        for (src, what, kind), n in sorted(mdns.items(), key=lambda kv: (kv[0][0], kv[0][1], -kv[1])):
            out.append("%7d  %-22s %-8s %s" % (n, src, what, kind))
        for (src, s), n in sorted(services.items(), key=lambda kv: (kv[0][0], -kv[1])):
            out.append("%7d  %-22s service  %s" % (n, src, s))
        for (src, q), n in sorted(flags.items(), key=lambda kv: (kv[0][0], -kv[1])):
            out.append("%7d  %-22s AirDrop TXT flags=%s" % (n, src, q))


SUMMARY_RULES = {
    "Radio": [r"\[[+-]\] ", r"\[\d\d:\d\d:\d\d\] HEARD ", r" {6,}\S"],
    "IO80211": [r"      (most peers in a state dump|cached|Peer Absence lines|passes to data mode): \d*$",
                r"      radio: "],
    "sharingd": [r"      Found BLE device: \d+$"],
    "IP on awdl0": [r"      (packets|TCP to 8770): \d+$", r"      no capture$"],
    "iPhone": [r"      [\d.]* ?[A-Za-z0-9,]* ?, pairing \w+$",
               r"      (IP packets on its interfaces|of them on its awdl0|TCP to 8770|log lines|"
               r"most peers in a state dump|Peer Absence lines|lines naming the Mac's awdl0): \d*$",
               r"      (no IP capture from the iPhone|no iPhone log)$"],
}


def summary_section(path, red, out):
    """The output of Tools/iphone_test.sh report: its own lines pass, copied log lines do not."""
    sect = None
    dropped = 0
    for l in read_lines(path):
        l = l.rstrip()
        m = re.match(r"== (.+)$", l)
        if m:
            if dropped:
                out.append("      (%d copied log lines left out)" % dropped)
            dropped = 0
            sect = m.group(1)
            out.append("-- " + sect if sect in SUMMARY_RULES else "-- (unknown section)")
            continue
        if not l:
            continue
        if sect == "IO80211" and l.startswith("AWDL ON:") and AWDL_ON_RE.match(l):
            out.append(AWDL_ON_RE.match(l).group(0))
            continue
        if sect == "IP on awdl0":
            m = re.match(r"\s*(\d+) (\S+) > (\S+)$", l)
            if m:
                src, sport = split_port(m.group(2))
                dst, dport = split_port(m.group(3))
                out.append("%7s %s > %s%s" % (m.group(1), red.ip(src), red.ip(dst), "." + dport if dport else ""))
                continue
        if any(re.match(rule, l) for rule in SUMMARY_RULES.get(sect, ())):
            out.append(red.text(l)[:400])
        else:
            dropped += 1
    if dropped:
        out.append("      (%d copied log lines left out)" % dropped)


def answer_text(key, value, red):
    value = clean(value, 1500 if key == "notes" else 120)
    if value in ("", "-"):
        return "not answered"
    if key in ("file_size", "seconds"):
        return re.sub(r"[^0-9A-Za-z .,/]", "", value)[:40]
    value = PHONE_RE.sub("<phone>", value)
    return red.text(value)


def cmd_build(run, work, dest, version):
    ident = load_identity(run, work)
    red = Redactor(ident)
    info = load_kv(os.path.join(run, "sysinfo.txt"))
    derived = derive_sysinfo(run)
    live = load_kv(os.path.join(work, "sysinfo_live.txt"))
    in_run = bool(info)

    def get(key):
        """A system fact: from the run, else from the machine now (and it says so)."""
        for d in (info, derived):
            if d.get(key):
                return d[key], ""
        if live.get(key):
            return live[key], "   [read when the report was written, not during the run]"
        return "", ""

    def row(label, key, fmt="%s"):
        v, origin = get(key)
        out.append("%-24s %s%s" % (label + ":", fmt % clean(str(v), 200) if v else "not recorded", origin if v else ""))

    out = []
    out.append("AWDL / AirDrop test report")
    out.append("For a new issue at " + ISSUES)
    out.append("")
    out.append("Addresses are replaced: self-N is an address of this Mac, peer-N of another device, and")
    out.append("fe80::<peer-N> the link-local address built from it. Names of people, computers, devices")
    out.append("and networks are left out. System log evidence is given as counts, never as text.")

    out.append("")
    out.append("== 1. Report and system")
    out.append("%-24s %s" % ("tool version:", version))
    out.append("%-24s %s" % ("report written (UTC):", time.strftime("%Y-%m-%d %H:%M", time.gmtime())))
    m = re.search(r"_(\d{4})(\d\d)(\d\d)_(\d\d)(\d\d)(\d\d)$", os.path.basename(run.rstrip("/")))
    if info.get("date_utc"):
        out.append("%-24s %s" % ("run started (UTC):", clean(info["date_utc"], 40)))
    elif m:
        out.append("%-24s %s-%s-%s %s:%s (local time, from the folder name)" % (("run started:",) + m.groups()[:5]))
    cap = read_lines(os.path.join(run, "capture.txt"))
    stamps = [l[1:9] for l in cap if re.match(r"\[\d\d:\d\d:\d\d\] ", l)]
    if len(stamps) >= 2:
        t0, t1 = [int(s[:2]) * 3600 + int(s[3:5]) * 60 + int(s[6:8]) for s in (stamps[0], stamps[-1])]
        out.append("%-24s %d min %02d s" % ("capture length:", ((t1 - t0) % 86400) // 60, ((t1 - t0) % 86400) % 60))
    v, origin = get("macos_build")
    ver, origin2 = get("macos_version")
    if origin2 and not origin:
        ver = ""   # the version read now may not be the one of the run
    out.append("%-24s %s%s" % ("macOS:", ((ver + " ") if ver else "") + ("(%s)" % v if v else "not recorded"), origin))
    hw = [x for x in (get("arch")[0], get("model")[0], get("cpu")[0]) if x]
    out.append("%-24s %s%s" % ("hardware:", clean(", ".join(hw), 200) or "not recorded", get("cpu")[1]))
    v, origin = get("boot_args")
    args = " ".join(t for t in v.split() if BOOTARG_RE.fullmatch(t))
    if args or "boot_args" in info or "boot_args" in derived:
        out.append("%-24s %s%s" % ("boot-args (rtl* only):", args or "none", origin if args else ""))
    else:
        out.append("%-24s not recorded" % "boot-args (rtl* only):")
    row("AirLayer gate", "gate")

    out.append("")
    out.append("== 2. Kexts and adapter")
    kexts = info.get("kext") or derived.get("kext")
    origin = ""
    if not kexts:
        kexts = live.get("kext") or []
        origin = "   [read when the report was written, not during the run]"
    for k in kexts:
        f = k.split()
        if len(f) >= 3 and re.fullmatch(r"[0-9A-Fa-f-]{36}", f[2]):
            out.append("%-24s version %s, UUID %s%s" % (clean(f[0], 40) + ":", clean(f[1], 20), f[2].upper(), origin))
        else:
            out.append("%-24s not loaded%s" % (clean(f[0], 40) + ":" if f else "kext:", origin))
    if not kexts:
        out.append("kexts: not recorded")
    v, origin = get("usb_id")
    name = get("usb_name")[0]
    out.append("%-24s %s%s%s" % ("adapter USB id:", clean(v, 12) or "not recorded",
                                 " (%s)" % clean(name, 60) if v and name else "", origin if v else ""))
    row("USB link speed", "usb_speed")
    row("awdl0 MTU", "awdl_mtu")
    if in_run:
        row("Bluetooth", "bluetooth")
        row("AirDrop receiving", "airdrop_mode")

    out.append("")
    out.append("== 3. Tester's answers")
    answers = load_kv(os.path.join(run, "answers.txt"))
    for key, label in QUESTIONS:
        out.append("%-54s %s" % (label + ":", answer_text(key, answers.get(key, ""), red)))

    out.append("")
    out.append("== 4. Driver state and counters")
    for tag in ("start", "end"):
        lines = uc_status(os.path.join(run, "uc_status_%s.txt" % tag), red)
        out.append("")
        out.append("-- Tools/test_uc status at the %s of the run" % tag)
        out.extend(lines or ["not sampled in this run (the folder was not made by Tools/awdl_report.sh run)"])
    out.append("")
    out.append("-- Thermal I/Q calibration counters (Tools/test_uc awdlcal)")
    for tag in ("start", "end"):
        lines = [l for l in read_lines(os.path.join(run, "uc_awdlcal_%s.txt" % tag)) if re.match(r"\[[+-]\] ", l)]
        out.append("%-6s %s" % (tag + ":", red.text(lines[0])[:200] if lines else "not sampled"))
    out.append("")
    out.append("-- Radio capture (Tools/test_uc sim capture): new sources, one counter line a minute, the end")
    periodic = [l for l in cap if re.match(r"\[\d\d:\d\d:\d\d\] tx ", l)]
    keep = set(periodic[::12] + periodic[-1:])
    shown = 0
    for l in cap:
        if l.startswith("[+] capture to "):
            continue
        if re.match(r"\[[+-]\] |\[\d\d:\d\d:\d\d\] (HEARD|ndp:) ", l) or l in keep:
            if shown < 400:
                out.append(red.text(l.rstrip())[:300])
            shown += 1
    if shown > 400:
        out.append("(%d more lines left out)" % (shown - 400))
    if not cap:
        out.append("no capture.txt in this run")
    ring = ring_lines(run)
    out.append("")
    out.append("-- Layer counters: first and last line of each kind in the ring, counted since boot")
    for prefix in ("awdl: tx psf ", "awdl: rx af ", "awdl: tx rate ", "awdl: A-MPDU frames ", "awdl: ba req ",
                   "awdl: dense presence "):
        hits = [(t, x) for t, x in ring if x.startswith(prefix)]
        if not hits:
            out.append("%-22s no line in the ring" % (prefix.strip() + ":"))
            continue
        out.append("first %s %s" % (hits[0][0], red.text(hits[0][1])[:400]))
        if len(hits) > 1:
            out.append("last  %s %s" % (hits[-1][0], red.text(hits[-1][1])[:400]))
    out.append("")
    out.append("-- Block Ack on AWDL data (from the layer's ring)")
    block_ack(ring, red, out)

    out.append("")
    out.append("== 5. Layer log ring (RTLAwdlLog)")
    acks = collections.Counter()
    body = []
    foreign = 0
    for t, x in ring:
        m = re.match(r"awdl: (set\w+|get\w+) acked$", x)
        if m:
            acks[m.group(1)] += 1
        elif x.startswith("awdl: "):
            body.append("%s %s" % (t, red.text(x)[:400]))
        else:
            foreign += 1
    if not ring:
        out.append("no ring in this run")
    else:
        out.append("time is when the run first saw the line (every 5 s); lines older than the run carry its start time")
        if acks:
            out.append("acknowledged requests, not listed: " + ", ".join("%s x%d" % kv for kv in sorted(acks.items())))
        if foreign:
            out.append("%d lines without the layer's prefix left out" % foreign)
        size = sum(len(l) + 1 for l in body)
        if size > RING_CAP:
            head, tail, used = [], [], 0
            for l in body:
                if used + len(l) > RING_CAP // 5:
                    break
                head.append(l)
                used += len(l) + 1
            used = 0
            for l in reversed(body[len(head):]):
                if used + len(l) > RING_CAP * 4 // 5:
                    break
                tail.append(l)
                used += len(l) + 1
            body = head + ["(%d lines left out here)" % (len(body) - len(head) - len(tail))] + tail[::-1]
        out.extend(body)

    out.append("")
    out.append("== 6. Run summary (Tools/iphone_test.sh report)")
    out.append("")
    summary_section(os.path.join(work, "summary.txt"), red, out)

    out.append("")
    out.append("== 7. System log evidence (counts of known events, no log text)")
    out.append("")
    out.append("-- This Mac")
    r = scan_log(os.path.join(run, "system.log"), red)
    if r is None:
        out.append("no system.log in this run")
    else:
        log_section(r, out)
    r = scan_log(os.path.join(run, "iphone_system.log"), red)
    if r is not None:
        out.append("")
        out.append("-- The Apple device, from its own log over USB")
        log_section(r, out)
    out.append("")
    out.append("-- IP on awdl0")
    ip_section(os.path.join(work, "ip.txt"), red, out)

    text = red.final("\n".join(out) + "\n")
    tail = ["", "== 8. Redaction"]
    tail.append("addresses of this Mac known: %d, other devices given a pseudonym: %d, other addresses: %d"
                % (len(red.selfs), len(red.peers), len(red.others)))
    for k in ("names replaced", "home paths replaced", "e-mail addresses replaced", "hex dumps left out"):
        tail.append("%s: %d" % (k, red.count[k]))
    tail.append("kept as they are: broadcast, multicast (33:33:*, 01:00:5e:*), the AWDL BSSID 00:25:00:ff:94:73")
    tail.append("selftest-address is the source of the frame the capture injects to test itself")
    text += red.final("\n".join(tail) + "\n")
    with open(dest, "w") as f:
        f.write(text)
    return 0


def cmd_verify(report, run, work):
    """Independent re-scan of a written report. Prints kind and line, never the value."""
    ident = load_identity(run, work)
    kinds = {"user": "user name", "host": "computer or host name", "computer": "computer or host name",
             "fullname": "personal name", "word": "personal name", "network": "network name",
             "device": "device name", "btname": "device name", "id": "serial number or hardware id"}
    needles = []
    for kind, s in private_strings(ident):
        needles.append((kinds[kind], re.sub(r"[ _-]", " ", s.lower().replace("\u2019", "'"))))
    bare = set(m.replace(":", "") for m in ident.known | set(ident.macs))
    found = []
    in_answers = False
    try:
        lines = open(report, errors="replace").read().splitlines()
    except OSError:
        print("cannot read the report")
        return 1
    for no, line in enumerate(lines, 1):
        hits = set()
        if line.startswith("== "):
            in_answers = line.startswith("== 3.")
        for rx in (MAC_COLON, MAC_DASH):
            for m in rx.finditer(line):
                n = norm_mac(m.group(0))
                if n and not mac_kept(n):
                    hits.add("MAC address")
        low = line.lower()
        if any(b in low for b in bare):
            hits.add("MAC address without separators")
        if HEXDUMP_RE.search(line):
            hits.add("raw bytes in hex")
        if re.search(r"fe80::?[0-9a-f]", low):
            hits.add("IPv6 link-local address")
        if "/users/" in low:
            hits.add("home path (/Users/)")
        if EMAIL_RE.search(line):
            hits.add("e-mail address")
        if in_answers and not line.startswith(("file size:", "seconds:")) and PHONE_RE.search(line.split(":", 1)[-1]):
            hits.add("phone number")
        if any(not ipv4_public(m) for m in IPV4_RE.finditer(line)):
            hits.add("IPv4 address")
        flat = re.sub(r"[ _-]", " ", low.replace("\u2019", "'"))
        for kind, s in needles:
            at = flat.find(s)
            while at >= 0:
                before = flat[at - 1] if at else " "
                after = flat[at + len(s)] if at + len(s) < len(flat) else " "
                if not (before.isalnum() and before.isascii()) and not (after.isalnum() and after.isascii()):
                    hits.add(kind)
                    break
                at = flat.find(s, at + 1)
        for h in sorted(hits):
            found.append((no, h))
    for no, h in found:
        print("  line %d: %s" % (no, h))
    return 1 if found else 0


def cmd_probe(mode):
    """The adapter in the IORegistry, from the two `ioreg -a` lists of ioreg_probe on stdin."""
    lists = []
    for part in sys.stdin.buffer.read().split(b"</plist>"):
        if b"<plist" not in part:
            continue
        try:
            lists.append(plistlib.loads(part.lstrip() + b"</plist>"))
        except Exception:
            lists.append([])
    devs = lists[0] if lists else []
    props = {}
    for o in lists[1] if len(lists) > 1 and isinstance(lists[1], list) else []:
        if isinstance(o, dict) and o.get("IOObjectClass") == "RTL8814Native":
            props = o

    def find(o, chain):
        if isinstance(o, list):
            for x in o:
                r = find(x, chain)
                if r:
                    return r
        elif isinstance(o, dict):
            if o.get("IOObjectClass") == "RTL8814Native":
                return chain + [o]
            return find(o.get("IORegistryEntryChildren", []), chain + [o])
        return None

    def walk(o):
        yield o
        for c in o.get("IORegistryEntryChildren", []):
            yield from walk(c)

    def safe(s, limit):
        return re.sub(r"[^A-Za-z0-9 ._+()/,:-]", "", str(s)).strip()[:limit]

    chain = find(devs, [])
    if not chain:
        print("adapter=0")
        return 0
    node = chain[-1]
    usb = [o for o in chain[:-1] if "idVendor" in o and "idProduct" in o and "Device Speed" in o]
    print("adapter=1")
    if usb:
        d = usb[-1]
        print("usb_id=%04x:%04x" % (int(d["idVendor"]), int(d["idProduct"])))
        print("usb_name=" + safe("%s %s" % (d.get("USB Vendor Name", ""), d.get("USB Product Name", "")), 60))
        names = {0: "Low-Speed", 1: "Full-Speed", 2: "High-Speed", 3: "SuperSpeed", 4: "SuperSpeed+"}
        speed = names.get(d.get("Device Speed"), "unknown")
        bps = d.get("UsbLinkSpeed")
        if isinstance(bps, int) and bps > 0:
            speed += " (%g Gb/s)" % (bps / 1e9) if bps >= 1e9 else " (%g Mb/s)" % (bps / 1e6)
        print("usb_speed=" + speed)
        serial = d.get("USB Serial Number") or d.get("kUSBSerialNumberString")
        if mode == "private" and serial:
            print("usb_serial=" + str(serial).strip())
    if props.get("RTLAirGate"):
        print("gate=" + safe(props["RTLAirGate"], 200))
    for o in walk(node):
        cls = o.get("IOObjectClass")
        if cls == "RTLAirInterface":
            for c in walk(o):
                name = str(c.get("BSD Name") or c.get("IORegistryEntryName") or "")
                if re.fullmatch(r"en\d+", name):
                    print("infra_if=" + name)
                    break
        elif cls == "RTLAwdInterface":
            name = str(o.get("BSD Name") or "")
            if re.fullmatch(r"[a-z]+\d+", name):
                print("awdl_if=" + name)
    return 0


def main(argv):
    if len(argv) >= 6 and argv[1] == "build":
        return cmd_build(argv[2], argv[3], argv[4], argv[5])
    if len(argv) >= 5 and argv[1] == "verify":
        return cmd_verify(argv[2], argv[3], argv[4])
    if len(argv) >= 3 and argv[1] == "probe":
        return cmd_probe(argv[2])
    return 64


sys.exit(main(sys.argv))
PYEOF

py() { python3 -I -c "$PY_PROG" "$@"; }

ioreg_probe() {   # public | private (adds the adapter's USB serial, for the redaction only)
	# Two property lists: the USB tree with the adapter under it, then the driver's own properties.
	{ ioreg -a -r -c IOUSBHostDevice; ioreg -a -r -d 1 -c RTL8814Native; } 2>/dev/null | py probe "$1"
}

bluetooth_state() {
	system_profiler SPBluetoothDataType 2>/dev/null | /usr/bin/grep -q 'State: On' && echo on || echo off
}

airdrop_mode() { as_owner defaults read com.apple.sharingd DiscoverableMode 2>/dev/null | tr -cd 'A-Za-z '; }

# Facts about the system that may be published, one key=value per line.
collect_sysinfo() {
	local id info
	printf 'tool_version=%s\n' "$VERSION"
	printf 'date_utc=%s\n' "$(date -u '+%Y-%m-%d %H:%M')"
	printf 'macos_version=%s\n' "$(sw_vers -productVersion)"
	printf 'macos_build=%s\n' "$(sw_vers -buildVersion)"
	printf 'arch=%s\n' "$(uname -m)"
	printf 'model=%s\n' "$(sysctl -n hw.model 2>/dev/null)"
	printf 'cpu=%s\n' "$(sysctl -n machdep.cpu.brand_string 2>/dev/null | tr -s ' ' | sed 's/ $//')"
	printf 'boot_args=%s\n' "$(rtl_boot_args)"
	for id in "$DRIVER_ID" "$LAYER_ID"; do
		info="$(kext_info "$id")"
		printf 'kext=%s %s\n' "${id##*.}" "${info:-not loaded}"
	done
	ioreg_probe public
	printf 'awdl_mtu=%s\n' "$(awdl_mtu)"
	printf 'bluetooth=%s\n' "$(bluetooth_state)"
	printf 'airdrop_mode=%s\n' "$(airdrop_mode)"
}

# What identifies this Mac and its owner. Never published: the redaction and the self-check
# read it to know what to look for.
collect_identity() {
	local i a u
	u="$(owner_name)"
	printf 'user\t%s\n' "$u"
	printf 'fullname\t%s\n' "$(id -F "$u" 2>/dev/null)"
	printf 'computer\t%s\n' "$(scutil --get ComputerName 2>/dev/null)"
	printf 'host\t%s\n' "$(scutil --get LocalHostName 2>/dev/null)"
	a="$(scutil --get HostName 2>/dev/null)" && printf 'host\t%s\n' "$a"
	printf 'host\t%s\n' "$(hostname 2>/dev/null)"
	# ifconfig with no interface fails on some machines: one interface at a time.
	for i in $(ifconfig -l 2>/dev/null); do
		a="$(ifconfig "$i" 2>/dev/null | awk '$1 == "ether" {print $2; exit}')"
		[ -n "$a" ] && printf 'mac\t%s\t%s\n' "$a" "$i"
	done
	ioreg -rd1 -c IOPlatformExpertDevice 2>/dev/null |
		awk -F'"' '/IOPlatformSerialNumber/ {print "serial\t" $4} /IOPlatformUUID/ {print "uuid\t" $4}'
	ioreg_probe private | awk -F= '$1 == "usb_serial" {print "usbserial\t" $2}'
}

cmd_check() {
	local a probe id info args want
	ok "macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion)), $(uname -m)"
	have_python && ok "python3 available" ||
		bad "python3 (it comes with the Command Line Tools: xcode-select --install)"

	for id in "$DRIVER_ID" "$LAYER_ID"; do
		info="$(kext_info "$id")"
		if [ -n "$info" ]; then
			ok "${id##*.} loaded, version ${info% *}, UUID ${info#* }"
		else
			bad "${id##*.} is not loaded"
		fi
	done

	probe=""
	have_python && probe="$(ioreg_probe public)"
	if [ "$(field "$probe" adapter)" = 1 ]; then
		ok "adapter on USB $(field "$probe" usb_id) ($(field "$probe" usb_name)), $(field "$probe" usb_speed)"
		case "$(field "$probe" usb_speed)" in
		Super*) ;;
		*) note "not a SuperSpeed link: AirDrop works, with less throughput; a USB 3 port on the board helps" ;;
		esac
		a="$(field "$probe" gate)"
		case "$a" in
		open-*) ok "AirLayer gate: $a" ;;
		"") note "the driver publishes no AirLayer gate state" ;;
		*) bad "AirLayer gate: $a" ;;
		esac
	elif have_python; then
		bad "no adapter attached to RTL8814Native (unplugged, or the kext is not loaded)"
	fi

	# ifconfig exits non-zero on awdl0 (SIOCGIFXMEDIA) with the interface fine.
	if ifconfig awdl0 2>/dev/null | /usr/bin/grep -q ether; then
		a="$(awdl_mtu)"
		[ "$a" = "$AWDL_MTU" ] && ok "awdl0 present, MTU $a" ||
			bad "awdl0 present with MTU ${a:-unknown}, expected $AWDL_MTU: the layer should set it"
	else
		bad "no awdl0 interface: the AirLayer did not create it (boot-args rtlair, rtlawdl)"
	fi

	args="$(rtl_boot_args)"
	note "boot-args of the kexts: ${args:-none}"
	for want in rtlawdl_tx=1 rtlawdl_sim=1; do
		case " $args " in
		*" $want "*) ;;
		*) case "$want" in
			rtlawdl_tx=1) bad "boot-arg $want: without it the Mac sends no AWDL frames and no device can see it" ;;
			*) bad "boot-arg $want: without it 'run' cannot capture the AWDL frames" ;;
			esac ;;
		esac
	done

	if [ -x Tools/test_uc ] && [ ! Tools/test_uc.c -nt Tools/test_uc ]; then
		ok "Tools/test_uc built"
		a="$(Tools/test_uc status 2>/dev/null)"
		case "$a" in
		*"ASSOCIATED"*)
			note "Wi-Fi associated on channel $(printf '%s\n' "$a" | sed -n 's/.*Current Channel *: \([0-9]*\).*/\1/p'): the radio is shared with that network during the test" ;;
		*"DISCONNECTED"*) note "Wi-Fi not associated: the radio stays on the AWDL channels, the simplest case" ;;
		*) bad "Tools/test_uc cannot reach the driver" ;;
		esac
	else
		bad "Tools/test_uc missing or older than its source; build it with: $TEST_UC_BUILD"
	fi

	[ "$(bluetooth_state)" = on ] && ok "Bluetooth on" || bad "Bluetooth is off: AirDrop finds devices over Bluetooth first"
	a="$(airdrop_mode)"
	case "$a" in
	Everyone) ok "AirDrop receiving: Everyone" ;;
	"Contacts Only") ok "AirDrop receiving: Contacts Only"
		note "Everyone is the sure setting for a test (Finder > AirDrop > Allow me to be discovered by)" ;;
	*) bad "AirDrop receiving is ${a:-unknown}: set it to Everyone in Finder > AirDrop" ;;
	esac
	pgrep -q -x awdlsim && bad "awdlsim is running: a test with a real device needs it stopped"

	echo
	if [ "$PASSED" = 1 ]; then
		echo "READY: sudo Tools/awdl_report.sh run"
	else
		echo "NOT READY: see the MISSING lines"
	fi
	[ "$PASSED" = 1 ]
}

answer_lookup() {   # key, --answers file or empty, run dir
	local key="$1" file="$2" dir="$3" var v=""
	[ -n "$file" ] && v="$(sed -n "s/^[[:space:]]*$key[[:space:]]*=[[:space:]]*//p" "$file" | tail -1)"
	if [ -z "$v" ]; then
		var="AWDL_REPORT_$(printf '%s' "$key" | tr a-z A-Z)"
		v="${!var:-}"
	fi
	[ -z "$v" ] && [ -f "$dir/answers.txt" ] && v="$(sed -n "s/^$key=//p" "$dir/answers.txt" | tail -1)"
	printf '%s' "$v" | tr -d '\r' | head -1
}

answer_norm() {   # key, value
	local low
	low="$(printf '%s' "$2" | tr A-Z a-z | sed 's/^ *//; s/ *$//')"
	case "$1" in
	direction)
		case "$low" in
		1|device-to-mac|"device to mac") echo "device to Mac" ;;
		2|mac-to-device|"mac to device") echo "Mac to device" ;;
		3|both) echo both ;;
		*) printf '%s\n' "$2" ;;
		esac ;;
	mac_in_device_sheet|device_in_mac_sheet|transfer_started|transfer_finished|wifi_associated)
		case "$low" in
		y|yes) echo yes ;;
		n|no) echo no ;;
		*) printf '%s\n' "$2" ;;
		esac ;;
	wifi_band)
		case "$low" in
		2|2.4*|24*) echo "2.4 GHz" ;;
		5*) echo "5 GHz" ;;
		6*) echo "6 GHz" ;;
		*) printf '%s\n' "$2" ;;
		esac ;;
	*) printf '%s\n' "$2" ;;
	esac
}

# 0 when the reply is one the question offers. Free-text questions take anything, and
# - (no answer) is always accepted.
answer_ok() {   # key, value
	local low
	low="$(printf '%s' "$2" | tr A-Z a-z | sed 's/^ *//; s/ *$//')"
	[ "$low" = "-" ] && return 0
	case "$1" in
	direction)
		case "$low" in
		1|2|3|both|device-to-mac|"device to mac"|mac-to-device|"mac to device") return 0 ;;
		esac
		return 1 ;;
	mac_in_device_sheet|device_in_mac_sheet)
		case "$low" in
		y|yes|n|no|"not tried") return 0 ;;
		esac
		return 1 ;;
	transfer_started|transfer_finished|wifi_associated)
		case "$low" in
		y|yes|n|no) return 0 ;;
		esac
		return 1 ;;
	wifi_band)
		case "$low" in
		2|2.4*|24*|5*|6*|unknown) return 0 ;;
		esac
		return 1 ;;
	seconds)
		printf '%s' "$low" | /usr/bin/grep -Eq '^[0-9]+([.,][0-9]+)?( *(s|sec|secs|seconds))?$' ;;
	*) return 0 ;;
	esac
}

question() {
	case "$1" in
	device_model) echo "Apple device model (for example iPhone 15 Pro, iPad Air M2)" ;;
	device_os) echo "Its OS version (for example iOS 18.5)" ;;
	direction) echo "Direction tested: 1 = device to Mac, 2 = Mac to device, 3 = both" ;;
	mac_in_device_sheet) echo "Did the Mac appear in the device's AirDrop sheet? (yes / no / not tried)" ;;
	device_in_mac_sheet) echo "Did the device appear in the Mac's AirDrop window? (yes / no / not tried)" ;;
	transfer_started) echo "Did a transfer start? (yes / no)" ;;
	transfer_finished) echo "Did it finish? (yes / no)" ;;
	file_size) echo "File size (for example 64 MB)" ;;
	seconds) echo "Seconds it took (a number)" ;;
	wifi_associated) echo "Was the Mac's Wi-Fi connected to a network during the test? (yes / no)" ;;
	wifi_band) echo "On which band is that network? (2.4 / 5 / 6 / unknown)" ;;
	notes) echo "Anything else worth knowing, in one line (no names; Return to skip)" ;;
	esac
}

# The answers end in <run dir>/answers.txt. Without a terminal nothing is asked and a missing
# answer stays empty ("not answered" in the report).
collect_answers() {   # run dir, --answers file or empty
	local dir="$1" file="$2" key v tmp interactive=0 asked=0
	[ -t 0 ] && interactive=1
	tmp="$WORK/answers.new"
	: > "$tmp"
	for key in $ANSWER_KEYS; do
		v="$(answer_lookup "$key" "$file" "$dir")"
		case "$key" in
		transfer_finished|file_size|seconds)
			/usr/bin/grep -qx 'transfer_started=no' "$tmp" && [ -z "$v" ] && v="-" ;;
		wifi_band)
			/usr/bin/grep -qx 'wifi_associated=no' "$tmp" && [ -z "$v" ] && v="-" ;;
		esac
		if [ -z "$v" ] && [ "$interactive" = 1 ]; then
			[ "$asked" = 0 ] && printf '\nA few questions about the test. They go into the report as typed: no names.\n' >&2
			asked=1
			printf '\n%s\n> ' "$(question "$key")" >&2
			while :; do
				IFS= read -r v || { v=""; break; }
				# An empty reply is an answer too: it is not asked again by the next pack.
				[ -z "$v" ] && break
				answer_ok "$key" "$v" && break
				printf 'Not one of the choices. Type one of them, or Return to skip.\n> ' >&2
			done
			[ -z "$v" ] && v="-"
		fi
		printf '%s=%s\n' "$key" "$(answer_norm "$key" "$v")" >> "$tmp"
	done
	{
		echo "# Answers of the tester for Tools/awdl_report.sh pack, one key=value per line."
		echo "# An empty value is asked again; - means no answer."
		cat "$tmp"
	} > "$dir/answers.txt"
}

live_identity() {   # into $WORK
	collect_identity > "$WORK/identity_live.txt"
	system_profiler SPBluetoothDataType > "$WORK/bt_live.txt" 2>/dev/null
}

make_work() {
	WORK="$(mktemp -d -t awdl_report)" || exit 1
	# Its files are raw: they go away with the script.
	trap 'rm -rf "$WORK"' EXIT
}

rejected() {   # file moved aside
	echo
	echo "REJECTED: the self-check found the kinds of private data listed above."
	echo "The file was renamed to $1 and must not be posted."
	echo "Please report the kinds and line numbers (not the content) at $ISSUES_URL"
}

cmd_pack() {
	local dir="" answers="" out size
	while [ $# -gt 0 ]; do
		case "$1" in
		--answers) [ $# -ge 2 ] || { usage; exit 1; }; answers="$(abs_path "$2")"; shift 2 ;;
		--answers=*) answers="$(abs_path "${1#--answers=}")"; shift ;;
		-*) usage; exit 1 ;;
		*) dir="$(abs_path "$1")"; shift ;;
		esac
	done
	[ -n "$dir" ] || { usage; exit 1; }
	[ -d "$dir" ] || { echo "no run folder $dir"; exit 1; }
	dir="$(cd "$dir" && pwd)"
	[ -f "$dir/capture.txt" ] || [ -f "$dir/system.log" ] || [ -f "$dir/ring.txt" ] ||
		{ echo "$dir is not a run folder (no capture.txt, system.log or ring.txt)"; exit 1; }
	[ -z "$answers" ] || [ -f "$answers" ] || { echo "no answers file $answers"; exit 1; }
	need_python
	[ -n "$WORK" ] || make_work

	collect_answers "$dir" "$answers"
	echo
	echo "Writing the report..."
	Tools/iphone_test.sh report "$dir" > "$WORK/summary.txt" 2>&1
	[ -s "$dir/awdl0_ip.pcap" ] && tcpdump -r "$dir/awdl0_ip.pcap" -n -tt > "$WORK/ip.txt" 2>/dev/null
	collect_sysinfo > "$WORK/sysinfo_live.txt"
	live_identity

	out="$dir/awdl_report.txt"
	rm -f "$out" "$dir/awdl_report.REJECTED.txt"
	py build "$dir" "$WORK" "$WORK/report.txt" "$VERSION" ||
		{ echo "the report could not be written"; exit 1; }
	py verify "$WORK/report.txt" "$dir" "$WORK" || out="$dir/awdl_report.REJECTED.txt"
	mv "$WORK/report.txt" "$out"
	if [ "$(id -u)" = 0 ]; then
		chown "$(stat -f %Su "$dir")" "$out" "$dir/answers.txt"
	fi
	case "$out" in
	*.REJECTED.txt) rejected "$out"; exit 2 ;;
	esac
	size=$(( ($(wc -c < "$out") + 1023) / 1024 ))
	cat <<-EOF

	Report: $out ($size KB, self-check passed)

	- Everything else in $dir is raw data with names and addresses:
	  keep it private, do not upload it.
	- Read the report before posting it. It is plain text. After editing it by hand:
	  Tools/awdl_report.sh verify "$out"
	- Attach or paste it into a new issue at $ISSUES_URL
	EOF
}

cmd_verify() {
	local file="${1:-}" moved
	[ -n "$file" ] || { usage; exit 1; }
	file="$(abs_path "$file")"
	[ -f "$file" ] || { echo "no file $file"; exit 1; }
	need_python
	make_work
	live_identity
	if py verify "$file" "$(dirname "$file")" "$WORK"; then
		echo "self-check passed: $file"
	else
		case "$file" in
		*.REJECTED.txt) moved="$file" ;;
		*.txt) moved="${file%.txt}.REJECTED.txt" ;;
		*) moved="$file.REJECTED.txt" ;;
		esac
		[ "$moved" = "$file" ] || mv "$file" "$moved"
		rejected "$moved"
		exit 2
	fi
}

sample_uc() {   # folder, start | end
	Tools/test_uc status > "$1/uc_status_$2.txt" 2>&1
	Tools/test_uc awdlcal > "$1/uc_awdlcal_$2.txt" 2>&1
}

cmd_run() {
	[ "$(id -u)" = 0 ] || { echo "needs sudo: sudo $0 run"; exit 1; }
	need_python
	[ -x Tools/test_uc ] || { echo "Tools/test_uc is not built: $TEST_UC_BUILD"; exit 1; }
	[ -n "$(kext_info "$DRIVER_ID")" ] || { echo "RTL8814Native is not loaded: see Tools/awdl_report.sh check"; exit 1; }
	# The frame capture keeps Tools/iphone_test.sh run going: without its port the run ends at once.
	case " $(rtl_boot_args) " in
	*" rtlawdl_sim=1 "*) ;;
	*) echo "boot-arg rtlawdl_sim=1 is missing (the frame capture needs it, and a reboot): see Tools/awdl_report.sh check"; exit 1 ;;
	esac

	local owner home dir
	owner="$(owner_name)"
	home="$(eval echo "~$owner")"
	make_work
	collect_sysinfo > "$WORK/sysinfo.txt"
	collect_identity > "$WORK/private_identity.txt"
	sample_uc "$WORK" start
	: > "$WORK/marker"

	cat <<-EOF
	Collecting starts now. Do the AirDrop test while it runs:

	  1. Apple device: Wi-Fi and Bluetooth on, unlocked, within a metre of the adapter,
	     AirDrop receiving set to "Everyone for 10 Minutes".
	  2. Device to Mac: share a photo by AirDrop and pick this Mac; accept on the Mac.
	  3. Mac to device: Finder > AirDrop, drag a file onto the device. The run folder
	     has two test files, envio_chico.txt (small) and envio_1mb.txt (1 MB).
	  4. Note the size of each file and the seconds it takes.
	  5. If nothing shows up, leave it running a few minutes with the AirDrop sheet open.

	Press Ctrl-C once when done.

	EOF
	# A handler, not "ignore": an ignored signal would be inherited and the child could not trap it.
	trap ':' INT
	Tools/iphone_test.sh run
	trap - INT

	dir="$(find "$home/Desktop" -maxdepth 1 -type d -name 'iphone_*' -newer "$WORK/marker" 2>/dev/null | sort | tail -1)"
	[ -n "$dir" ] || { echo "no run folder was created: see the messages above"; exit 1; }
	dir="$(cd "$dir" && pwd)"
	sample_uc "$dir" end
	mv "$WORK/sysinfo.txt" "$WORK/private_identity.txt" "$WORK"/uc_*_start.txt "$dir/"
	chown -R "$owner" "$dir"
	echo
	if /usr/bin/grep -q '^\[-\] capture' "$dir/capture.txt" 2>/dev/null && ! /usr/bin/grep -q '^\[+\] end' "$dir/capture.txt"; then
		echo "The frame capture did not start, so nothing was collected during the test:"
		/usr/bin/grep '^\[-\]' "$dir/capture.txt" | head -3
		echo "The report is written all the same: it says how far the run got."
		echo
	fi
	echo "Run folder: $dir"
	echo "If the questions below are interrupted: Tools/awdl_report.sh pack \"$dir\""
	cmd_pack "$dir"
}

case "${1:-}" in
check)  cmd_check ;;
run)    cmd_run ;;
pack)   shift; cmd_pack "$@" ;;
verify) cmd_verify "${2:-}" ;;
*)      usage; exit 1 ;;
esac
