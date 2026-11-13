#!/usr/bin/env python3
"""Per-source summary of a frames.pcapng from Tools/iphone_test.sh run.

The TLV types the Mac sent, then for each sender: frames by kind, RSSI range, channels,
and for action frames the TLV types of the first PSF and MIF. AWDL action frames per Docs/AWDL_FRAME_WIRE_FORMAT.md:
24-byte 802.11 header, then 7f 00 17 f2 08 10 <subtype> 00, two u32 times, TLVs.
"""
import collections
import re
import struct
import sys

HDR = 24
AF_FIXED = 16
KINDS = {0: "PSF", 3: "MIF"}


def packets(path):
    d = open(path, "rb").read()
    i = 0
    while i + 12 <= len(d):
        ty, ln = struct.unpack_from("<II", d, i)
        if ln < 12 or i + ln > len(d):
            return  # a run cut short leaves a partial block at the end
        if ty == 6:
            hi, lo, cap = struct.unpack_from("<III", d, i + 12)
            p = d[i + 28 : i + 28 + cap]
            o = i + 28 + cap + (-cap % 4)
            comment = ""
            if o + 4 <= i + ln - 4:
                code, clen = struct.unpack_from("<HH", d, o)
                if code == 1:
                    comment = d[o + 4 : o + 4 + clen].decode("ascii", "replace")
            yield (hi << 32 | lo), p, comment
        i += ln


def tlv_types(body):
    out = []
    i = 0
    while i + 3 <= len(body):
        t, ln = body[i], struct.unpack_from("<H", body, i + 1)[0]
        out.append(t)
        i += 3 + ln
    return out


def mac(b):
    return b.hex(":")


def main(path):
    src = collections.defaultdict(
        lambda: {
            "kinds": collections.Counter(),
            "rssi": [],
            "ch": collections.Counter(),
            "tlvs": {},
            "first": None,
            "last": None,
        }
    )
    rx = tx = lost = 0
    own = {}
    to_us = collections.Counter()
    for t, p, comment in packets(path):
        if len(p) < HDR:
            continue
        is_rx = comment.startswith("air >")
        rx += is_rx
        tx += not is_rx
        lost += "overrun" in comment
        if not is_rx:
            if p[0] & 0x0C == 0 and p[HDR : HDR + 6] == b"\x7f\x00\x17\xf2\x08\x10":
                # The longest of each kind: a MIF's services come and go.
                kind = KINDS.get(p[HDR + 6], "AF%d" % p[HDR + 6])
                types = tlv_types(p[HDR + AF_FIXED :])
                if len(types) > len(own.get(kind, [])):
                    own[kind] = types
            continue
        s = src[mac(p[10:16])]
        s["first"] = s["first"] if s["first"] is not None else t
        s["last"] = t
        m = re.search(r"ch (\d+) rssi (-?\d+)", comment)
        if m:
            s["ch"][int(m.group(1))] += 1
            s["rssi"].append(int(m.group(2)))
        if p[4] & 1 == 0:
            to_us[mac(p[10:16])] += 1
        if p[0] & 0x0C == 0 and p[HDR : HDR + 6] == b"\x7f\x00\x17\xf2\x08\x10":
            sub = p[HDR + 6]
            kind = KINDS.get(sub, "AF%d" % sub)
            s["kinds"][kind] += 1
            s["tlvs"].setdefault(kind, tlv_types(p[HDR + AF_FIXED :]))
        else:
            s["kinds"]["data" if p[0] & 0x0C == 8 else "other"] += 1
    print("      frames: tx %d, rx %d, runs lost to the ring %d" % (tx, rx, lost))
    for kind, tlvs in own.items():
        print("      Mac %s TLVs %s" % (kind, tlvs))
    for name, s in sorted(src.items(), key=lambda kv: -sum(kv[1]["kinds"].values())):
        secs = (s["last"] - s["first"]) / 1e6
        rssi = s["rssi"] or [0]
        print(
            "      %s %.0f s, rssi %d..%d, ch %s, %s, unicast %d"
            % (name, secs, min(rssi), max(rssi), dict(s["ch"]), dict(s["kinds"]), to_us[name])
        )
        for kind, tlvs in s["tlvs"].items():
            print("        %s TLVs %s" % (kind, tlvs))


if __name__ == "__main__":
    main(sys.argv[1])
