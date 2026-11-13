#!/usr/bin/env python3
"""Block Ack and rate report for an 802.11 monitor capture.

Reads a classic pcap (microsecond or nanosecond magic) or a pcapng file whose
link type is 127 (radiotap, then the 802.11 frame) and prints, for the two
directions between the Mac and the iPhone:

  * ADDBA request / response / DELBA action frames with their parameters,
  * BAR (control subtype 8) and BlockAck (control subtype 9) frames with the
    starting sequence number and bitmap length,
  * for every BlockAck, the gap in microseconds to the previous data frame
    from the peer, next to the airtime that frame needed (a hardware answer
    lands about one SIFS plus that airtime after it),
  * data frames: QoS or not, A-MSDU bit, retry bit, and A-MPDU membership from
    the radiotap A-MPDU status field (present bit 20), grouping by reference
    number; when the capture has no such field, a burst is inferred from
    consecutive data records instead, and the report says so,
  * the PHY rate per frame from the legacy rate field, the MCS field (bit 19)
    or the VHT field (bit 21), as a histogram per direction,
  * plain ACK frames (control subtype 13) with RA = peer, the proxy for a
    single-MPDU acknowledgement by the Mac,
  * a verdict line: hardware BlockAck seen, iPhone aggregating, dominant rate
    each way.

Usage:
    python3 -I ba_capture_report.py CAPTURE [--mac AA:BB:CC:DD:EE:FF]
                                          [--peer AA:BB:CC:DD:EE:FF]

Without --mac/--peer the two busiest unicast talkers on the AWDL BSSID are
inferred; the choice and its evidence are printed.

Standard library only.
"""

import argparse
import struct
import sys
from collections import Counter

AWDL_BSSID = "00:25:00:ff:94:73"
SIFS_US = 16.0
LIST_CAP = 20
LINKTYPE_RADIOTAP = 127

# radiotap field layout: present bit -> (alignment, size), alignment and size
# from the start of the radiotap header.  Size -1 marks a variable field.
RT_LAYOUT = {
    0: (8, 8),     # TSFT
    1: (1, 1),     # flags
    2: (1, 1),     # rate
    3: (2, 4),     # channel
    4: (2, 2),     # FHSS
    5: (1, 1),     # dBm antenna signal
    6: (1, 1),     # dBm antenna noise
    7: (4, 2),     # lock quality
    8: (2, 2),     # TX attenuation
    9: (2, 2),     # dB TX attenuation
    10: (1, 1),    # dBm TX power
    11: (1, 1),    # antenna
    12: (1, 1),    # dB antenna signal
    13: (1, 1),    # dB antenna noise
    14: (2, 2),    # RX flags
    15: (2, 2),    # TX flags
    16: (1, 1),    # RTS retries
    17: (1, 1),    # data retries
    18: (4, 8),    # XChannel
    19: (1, 3),    # MCS
    20: (4, 8),    # A-MPDU status
    21: (2, 12),   # VHT
    22: (12, 12),  # timestamp
    23: (2, 12),   # HE
    24: (2, 12),   # HE-MU
    25: (2, 6),    # HE-MU-other-user
    26: (2, 1),    # 0-length PSDU
    27: (2, 4),    # L-SIG
    28: (4, 4),    # TLV
    29: (2, 2),    # radiotap namespace
    30: (2, 6),    # vendor namespace
}

RT_NAMES = {
    0: "tsft", 1: "flags", 2: "rate", 3: "channel", 4: "fhss",
    5: "signal", 6: "noise", 7: "lock", 8: "txatten", 9: "txatten_db",
    10: "txpower", 11: "antenna", 12: "signal_db", 13: "noise_db",
    14: "rxflags", 15: "txflags", 16: "rts_retries", 17: "data_retries",
    18: "xchannel", 19: "mcs", 20: "ampdu", 21: "vht", 22: "timestamp",
    23: "he", 24: "he_mu", 25: "he_mu_other", 26: "zerolen", 27: "lsig",
    28: "tlv", 29: "namespace", 30: "vendor",
}

HT_MCS20_LGI = (6.5, 13.0, 19.5, 26.0, 39.0, 52.0, 58.5, 65.0)
VHT_MCS20_LGI = (6.5, 13.0, 19.5, 26.0, 39.0, 52.0, 58.5, 65.0, 78.0, 86.7)

PREAMBLE_US = {"legacy": 20.0, "ht": 36.0, "vht": 40.0}


# --------------------------------------------------------------------------
# little helpers


def norm_mac(text):
    digits = "".join(c for c in text.lower() if c in "0123456789abcdef")
    if len(digits) != 12:
        raise SystemExit("not a MAC address: %r" % text)
    return ":".join(digits[i:i + 2] for i in range(0, 12, 2))


def mac_str(raw):
    if len(raw) != 6:
        return "?"
    return ":".join("%02x" % b for b in raw)


def align_up(pos, alignment):
    if alignment <= 1:
        return pos
    return pos + (-pos % alignment)


def is_multicast(a):
    if not a:
        return True
    return bool(int(a[0:2], 16) & 0x01)


# --------------------------------------------------------------------------
# capture readers


def load_capture(path):
    with open(path, "rb") as fh:
        buf = fh.read()
    if len(buf) < 24:
        raise SystemExit("capture too short: %s" % path)
    if buf[:4] == b"\x0a\x0d\x0d\x0a":
        return _load_pcapng(buf, path)
    return _load_pcap(buf, path)


def _load_pcap(buf, path):
    magics = {
        b"\xd4\xc3\xb2\xa1": ("<", 1e-6),
        b"\xa1\xb2\xc3\xd4": (">", 1e-6),
        b"\x4d\x3c\xb2\xa1": ("<", 1e-9),
        b"\xa1\xb2\x3c\x4d": (">", 1e-9),
    }
    magic = buf[:4]
    if magic not in magics:
        raise SystemExit("not a pcap or pcapng file: %s" % path)
    endian, unit = magics[magic]
    ver_major, ver_minor, _tz, _sig, snaplen, linktype = struct.unpack_from(
        endian + "HHiIII", buf, 4)
    records = []
    off = 24
    cut = False
    while off + 16 <= len(buf):
        ts_sec, ts_frac, incl, orig = struct.unpack_from(endian + "IIII", buf, off)
        off += 16
        avail = len(buf) - off
        if incl > avail:
            incl = avail
            cut = True
        data = buf[off:off + incl]
        off += incl
        records.append({
            "ts": ts_sec + ts_frac * unit,
            "linktype": linktype,
            "data": data,
            "origlen": orig,
            "caplen": len(data),
            "trunc": len(data) < orig,
        })
        if cut:
            break
    meta = {
        "format": "pcap %d.%d" % (ver_major, ver_minor),
        "endian": "little" if endian == "<" else "big",
        "unit": "ns" if unit == 1e-9 else "us",
        "linktype": linktype,
        "snaplen": snaplen,
        "cut": cut,
        "interfaces": 1,
        "records": len(records),
    }
    return records, meta


def _pcapng_options(body, start):
    """Return option dict for the option area starting at start."""
    out = {}
    pos = start
    while pos + 4 <= len(body):
        code, length = struct.unpack_from("<HH", body, pos)
        pos += 4
        if code == 0:
            break
        value = body[pos:pos + length]
        pos += length + (-length % 4)
        out.setdefault(code, value)
    return out


def _load_pcapng(buf, path):
    records = []
    ifaces = []
    off = 0
    cut = False
    while off + 12 <= len(buf):
        btype, blen = struct.unpack_from("<II", buf, off)
        if blen < 12 or off + blen > len(buf):
            cut = True
            break
        body = buf[off + 8:off + blen - 4]
        if btype == 1:  # interface description
            if len(body) >= 8:
                linktype, _res, snaplen = struct.unpack_from("<HHI", body, 0)
                tsresol = 1e-6
                opts = _pcapng_options(body, 8)
                if 9 in opts and len(opts[9]) >= 1:
                    raw = opts[9][0]
                    if raw & 0x80:
                        tsresol = 2.0 ** (-(raw & 0x7f))
                    else:
                        tsresol = 10.0 ** (-raw)
                ifaces.append({"linktype": linktype, "snaplen": snaplen,
                               "tsresol": tsresol})
        elif btype == 6:  # enhanced packet
            if len(body) >= 20:
                iface, hi, lo, caplen, origlen = struct.unpack_from("<IIIII", body, 0)
                data = body[20:20 + caplen]
                if iface < len(ifaces):
                    info = ifaces[iface]
                else:
                    info = {"linktype": -1, "tsresol": 1e-6}
                records.append({
                    "ts": ((hi << 32) | lo) * info["tsresol"],
                    "linktype": info["linktype"],
                    "data": data,
                    "origlen": origlen,
                    "caplen": len(data),
                    "trunc": len(data) < origlen,
                })
        elif btype == 3:  # simple packet
            if len(body) >= 4:
                origlen = struct.unpack_from("<I", body, 0)[0]
                data = body[4:]
                info = ifaces[0] if ifaces else {"linktype": -1}
                records.append({
                    "ts": records[-1]["ts"] if records else 0.0,
                    "linktype": info.get("linktype", -1),
                    "data": data,
                    "origlen": origlen,
                    "caplen": len(data),
                    "trunc": len(data) < origlen,
                })
        off += blen
    meta = {
        "format": "pcapng",
        "linktype": ifaces[0]["linktype"] if ifaces else -1,
        "snaplen": ifaces[0]["snaplen"] if ifaces else 0,
        "cut": cut,
        "interfaces": len(ifaces),
        "records": len(records),
    }
    return records, meta


# --------------------------------------------------------------------------
# radiotap


def parse_radiotap(buf):
    info = {
        "present": [],
        "names": [],
        "payload": None,
        "hlen": None,
        "fcs": False,
        "rate_mbps": None,
        "rate_kind": None,
        "mcs": None,
        "vht": None,
        "ampdu_ref": None,
        "ampdu_flags": None,
        "freq": None,
        "signal": None,
        "flags": None,
        "problem": None,
    }
    if len(buf) < 8:
        info["problem"] = "radiotap shorter than 8 bytes"
        return info
    version, _pad, hlen = struct.unpack_from("<BBH", buf, 0)
    if version != 0:
        info["problem"] = "radiotap version %d" % version
        return info
    info["hlen"] = hlen
    present = []
    pos = 4
    while True:
        if pos + 4 > len(buf):
            info["problem"] = "present bitmap truncated"
            return info
        word = struct.unpack_from("<I", buf, pos)[0]
        pos += 4
        present.append(word)
        if not word & 0x80000000:
            break
        if len(present) > 8:
            info["problem"] = "runaway present bitmap chain"
            return info
    info["present"] = present
    if len(present) > 1:
        info["problem"] = "extended present bitmap (fields past bit 30 unknown)"
        info["names"] = _rt_names(present[0])
        return info
    word = present[0]
    for bit in range(31):
        if not word & (1 << bit):
            continue
        if bit not in RT_LAYOUT:
            info["problem"] = "unknown radiotap field bit %d" % bit
            return info
        alignment, size = RT_LAYOUT[bit]
        pos = align_up(pos, alignment)
        if bit == 28:
            info["problem"] = "radiotap TLV field"
            return info
        if pos + size > len(buf):
            info["problem"] = "radiotap field %s truncated" % RT_NAMES.get(bit, bit)
            return info
        raw = buf[pos:pos + size]
        pos += size
        info["names"].append(RT_NAMES.get(bit, str(bit)))
        if bit == 1:
            info["flags"] = raw[0]
            info["fcs"] = bool(raw[0] & 0x10)
        elif bit == 2:
            info["rate_mbps"] = raw[0] * 0.5
            info["rate_kind"] = "legacy"
        elif bit == 3:
            info["freq"] = struct.unpack_from("<H", raw, 0)[0]
        elif bit == 5:
            info["signal"] = struct.unpack_from("<b", raw, 0)[0]
        elif bit == 19:
            mcs_flags = raw[1]
            index = raw[2]
            bw = {0: 20, 1: 40, 2: 20, 3: 20}[mcs_flags & 0x03]
            sgi = bool(mcs_flags & 0x04)
            fmt = (mcs_flags >> 3) & 0x03
            rate = None
            if fmt in (0, 1):
                rate = ht_rate(index, bw, sgi)
            info["mcs"] = {
                "index": index,
                "streams": index // 8 + 1,
                "bw": bw,
                "sgi": sgi,
                "format": fmt,
                "mbps": rate,
            }
            if rate is not None:
                info["rate_mbps"] = rate
                info["rate_kind"] = "ht"
        elif bit == 20:
            word32 = struct.unpack_from("<I", raw, 0)[0]
            info["ampdu_ref"] = word32 & 0x00000fff
            info["ampdu_flags"] = struct.unpack_from("<H", raw, 4)[0]
        elif bit == 21:
            _known, vflags, bw_code, m0, m1, m2, m3 = struct.unpack_from(
                "<HBBBBBB", raw, 0)
            sgi = bool(vflags & 0x04)
            bw = {0: 20, 1: 40, 2: 80, 3: 160, 4: 80}.get(bw_code, 20)
            nss = 0
            mcs = 0
            for i, byte in enumerate((m0, m1, m2, m3)):
                if byte & 0x0f:
                    mcs = byte & 0x0f
                    nss = i + 1
            rate = vht_rate(nss, mcs, bw, sgi)
            info["vht"] = {"nss": nss, "mcs": mcs, "bw": bw, "sgi": sgi,
                           "mbps": rate}
            if rate is not None:
                info["rate_mbps"] = rate
                info["rate_kind"] = "vht"
    payload_at = hlen if 8 <= hlen <= len(buf) else pos
    info["payload"] = buf[payload_at:]
    return info


def _rt_names(word):
    return [RT_NAMES.get(bit, str(bit)) for bit in range(31) if word & (1 << bit)]


def ht_rate(index, bw, sgi):
    if index < 0 or index > 31:
        return None
    streams = index // 8 + 1
    rate = HT_MCS20_LGI[index % 8] * streams * (bw / 20.0)
    if sgi:
        rate *= 10.0 / 9.0
    return rate


def vht_rate(nss, mcs, bw, sgi):
    if nss < 1 or mcs > 9:
        return None
    rate = VHT_MCS20_LGI[mcs] * nss * (bw / 20.0)
    if sgi:
        rate *= 10.0 / 9.0
    return rate


def rate_label(info):
    if info["rate_kind"] == "vht" and info["vht"] and info["vht"]["mbps"]:
        v = info["vht"]
        return "VHT mcs%d/%d %dMHz %s %.1f" % (
            v["mcs"], v["nss"], v["bw"], "SGI" if v["sgi"] else "LGI", v["mbps"])
    if info["rate_kind"] == "ht" and info["mcs"] and info["mcs"]["mbps"]:
        m = info["mcs"]
        return "HT mcs%d %dMHz %s %.1f" % (
            m["index"], m["bw"], "SGI" if m["sgi"] else "LGI", m["mbps"])
    if info["rate_mbps"] is not None:
        return "legacy %.1f" % info["rate_mbps"]
    return "unknown"


# --------------------------------------------------------------------------
# 802.11


def parse_dot11(frame, fcs_present, truncated):
    data = frame
    if fcs_present and not truncated and len(data) >= 4:
        data = data[:-4]
    if len(data) < 10:
        return None
    fc = struct.unpack_from("<H", data, 0)[0]
    out = {
        "type": (fc >> 2) & 0x03,
        "sub": (fc >> 4) & 0x0f,
        "tods": bool(fc & 0x0100),
        "fromds": bool(fc & 0x0200),
        "retry": bool(fc & 0x0800),
        "protected": bool(fc & 0x4000),
        "order": bool(fc & 0x8000),
        "a1": None,
        "a2": None,
        "a3": None,
        "seq": None,
        "frag": None,
        "qos": False,
        "tid": None,
        "amsdu": False,
        "addba": None,
        "delba": None,
        "bar": None,
        "ba": None,
        "fc": fc,
        "len": len(data),
    }
    if out["type"] == 0:  # management
        if len(data) < 24:
            return out
        out["a1"] = mac_str(data[4:10])
        out["a2"] = mac_str(data[10:16])
        out["a3"] = mac_str(data[16:22])
        out["seq"] = struct.unpack_from("<H", data, 22)[0] >> 4
        if out["sub"] == 13 and len(data) >= 26:
            category = data[24]
            action = data[25]
            if category == 3:  # block ack
                if action == 0 and len(data) >= 33:
                    params = struct.unpack_from("<H", data, 27)[0]
                    out["addba"] = {
                        "kind": "req",
                        "dialog": data[26],
                        "amsdu": bool(params & 0x1),
                        "policy": (params >> 1) & 0x1,
                        "tid": (params >> 2) & 0x0f,
                        "buf": (params >> 6) & 0x3ff,
                        "timeout": struct.unpack_from("<H", data, 29)[0],
                        "ssn": struct.unpack_from("<H", data, 31)[0] >> 4,
                    }
                elif action == 1 and len(data) >= 31:
                    params = struct.unpack_from("<H", data, 29)[0]
                    out["addba"] = {
                        "kind": "resp",
                        "dialog": data[26],
                        "status": struct.unpack_from("<H", data, 27)[0],
                        "amsdu": bool(params & 0x1),
                        "policy": (params >> 1) & 0x1,
                        "tid": (params >> 2) & 0x0f,
                        "buf": (params >> 6) & 0x3ff,
                        "timeout": struct.unpack_from("<H", data, 31)[0],
                    }
                elif action == 2 and len(data) >= 30:
                    params = struct.unpack_from("<H", data, 26)[0]
                    out["delba"] = {
                        "initiator": bool(params & 0x0800),
                        "tid": (params >> 12) & 0x0f,
                        "reason": struct.unpack_from("<H", data, 28)[0],
                    }
    elif out["type"] == 1:  # control
        out["a1"] = mac_str(data[4:10])
        if out["sub"] in (8, 9) and len(data) >= 24:
            out["a2"] = mac_str(data[10:16])
        if out["sub"] == 8 and len(data) >= 20:
            ctrl = struct.unpack_from("<H", data, 16)[0]
            seq = struct.unpack_from("<H", data, 18)[0]
            out["bar"] = {
                "tid": (ctrl >> 12) & 0x0f,
                "compressed": bool(ctrl & 0x0004),
                "multi_tid": bool(ctrl & 0x0002),
                "ssn": seq >> 4,
                "frag": seq & 0x0f,
            }
        elif out["sub"] == 9 and len(data) >= 24:
            ctrl = struct.unpack_from("<H", data, 16)[0]
            seq = struct.unpack_from("<H", data, 18)[0]
            compressed = bool(ctrl & 0x0004)
            bitmap_bytes = 8 if compressed else 32
            have = len(data) - 20
            out["ba"] = {
                "tid": (ctrl >> 12) & 0x0f,
                "compressed": compressed,
                "multi_tid": bool(ctrl & 0x0002),
                "ack_policy": ctrl & 0x1,
                "ssn": seq >> 4,
                "frag": seq & 0x0f,
                "bitmap_len": bitmap_bytes,
                "bitmap_ok": have >= bitmap_bytes,
                "bitmap": data[20:20 + bitmap_bytes],
            }
    elif out["type"] == 2:  # data
        if len(data) < 24:
            return out
        out["a1"] = mac_str(data[4:10])
        out["a2"] = mac_str(data[10:16])
        out["a3"] = mac_str(data[16:22])
        word = struct.unpack_from("<H", data, 22)[0]
        out["seq"] = word >> 4
        out["frag"] = word & 0x0f
        out["qos"] = bool(out["sub"] & 0x08)
        off = 24
        if out["tods"] and out["fromds"]:
            off += 6
        if out["qos"] and len(data) >= off + 2:
            qc = struct.unpack_from("<H", data, off)[0]
            out["tid"] = qc & 0x0f
            out["amsdu"] = bool(qc & 0x0080)
        if out["qos"]:
            out["sub"] = 8  # fold to a single data label
    return out


# --------------------------------------------------------------------------
# frames


def build_frames(records):
    frames = []
    stats = Counter()
    for index, rec in enumerate(records):
        if rec["linktype"] not in (LINKTYPE_RADIOTAP, -1):
            stats["skipped_linktype"] += 1
            continue
        rt = parse_radiotap(rec["data"])
        if rt["problem"] and rt["payload"] is None:
            stats["radiotap_error"] += 1
            continue
        d11 = parse_dot11(rt["payload"], rt["fcs"], rec["trunc"])
        if d11 is None:
            stats["short_frame"] += 1
            continue
        d11["i"] = index
        d11["ts"] = rec["ts"]
        d11["trunc"] = rec["trunc"]
        d11["frame_len"] = rec["origlen"]
        d11["rate"] = rate_label(rt)
        d11["rate_mbps"] = rt["rate_mbps"]
        d11["rate_kind"] = rt["rate_kind"]
        d11["ampdu_ref"] = rt["ampdu_ref"]
        d11["ampdu_flags"] = rt["ampdu_flags"]
        d11["freq"] = rt["freq"]
        d11["signal"] = rt["signal"]
        d11["rt_names"] = rt["names"]
        d11["rt_problem"] = rt["problem"]
        frames.append(d11)
        stats["frames"] += 1
    return frames, stats


def frame_airtime_us(fr):
    if not fr["rate_mbps"]:
        return None
    kind = fr["rate_kind"] or "legacy"
    preamble = PREAMBLE_US.get(kind, 20.0)
    return preamble + 8.0 * fr["frame_len"] / (fr["rate_mbps"] * 1e6) * 1e6


# --------------------------------------------------------------------------
# address inference


def pick_addresses(frames, want_mac, want_peer):
    awdl = [f for f in frames
            if f["a3"] == AWDL_BSSID or f["a1"] == AWDL_BSSID]
    scope = awdl if awdl else frames
    def _pairs(types):
        out = Counter()
        for fr in scope:
            if fr["type"] not in types:
                continue
            if not fr["a1"] or not fr["a2"]:
                continue
            if is_multicast(fr["a1"]) or is_multicast(fr["a2"]):
                continue
            if fr["a1"] == fr["a2"]:
                continue
            out[(fr["a2"], fr["a1"])] += 1
        return out

    pairs = _pairs((2,))
    if not pairs:
        pairs = _pairs((0, 2))
    if not pairs:
        raise SystemExit("no unicast data or management traffic found")
    (best_src, best_dst), count = pairs.most_common(1)[0]
    if want_mac and want_peer:
        return want_mac, want_peer, {
            "note": "given on the command line",
            "pairs": pairs, "count": count, "scope": len(scope),
        }
    peer = want_peer or best_src
    mac = want_mac or best_dst
    if want_mac:
        mac = want_mac
        cand = [pair for pair in pairs if mac in pair]
        if cand:
            other = cand[0][0] if cand[0][1] == mac else cand[0][1]
            peer = want_peer or other
    if want_peer:
        peer = want_peer
        cand = [pair for pair in pairs if peer in pair]
        if cand:
            other = cand[0][0] if cand[0][1] == peer else cand[0][1]
            mac = want_mac or other
    note = {
        "note": "inferred: %d frames on the busiest unicast pair, %d of them "
                "with the AWDL BSSID in the header" % (count, len(awdl)),
        "pairs": pairs, "count": count, "scope": len(scope),
    }
    return mac, peer, note


# --------------------------------------------------------------------------
# analysis


def direction_frames(frames, src, dst):
    return [fr for fr in frames if fr["a2"] == src and fr["a1"] == dst]


def burst_report(frames, src, dst):
    """Return (explicit, bursts, sizes, fallback_used)."""
    data = direction_frames(frames, src, dst)
    explicit = any(fr["ampdu_ref"] is not None for fr in data)
    bursts = []
    if explicit:
        current = None
        for fr in data:
            ref = fr["ampdu_ref"]
            if ref is None:
                continue
            if current is None or current["ref"] != ref:
                current = {"ref": ref, "frames": []}
                bursts.append(current)
            current["frames"].append(fr)
    else:
        # no A-MPDU status field: a burst is a run of consecutive records of
        # the same direction with consecutive sequence numbers
        ordered = sorted(frames, key=lambda f: (f["ts"], f["i"]))
        current = None
        last_index = None
        for fr in ordered:
            if not (fr["type"] == 2 and fr["a2"] == src and fr["a1"] == dst):
                last_index = fr["i"] if fr["type"] != 2 or fr["a1"] != dst else last_index
                continue
            contiguous = (last_index is not None and fr["i"] == last_index + 1)
            continues = (current is not None and contiguous
                         and fr["seq"] == (current["frames"][-1]["seq"] + 1) % 4096
                         and fr["tid"] == current["frames"][-1]["tid"])
            if continues:
                current["frames"].append(fr)
            else:
                current = {"ref": None, "frames": [fr]}
                bursts.append(current)
            last_index = fr["i"]
    sizes = Counter(len(b["frames"]) for b in bursts)
    return explicit, bursts, sizes, not explicit


def ack_count(frames, ra):
    return sum(1 for fr in frames
               if fr["type"] == 1 and fr["sub"] == 13 and fr["a1"] == ra)


def ba_gaps(frames, mac, peer):
    """For every BlockAck from mac to peer: the previous peer data frame,
    the gap in microseconds and whether that frame is the record just
    before it (the only case where a SIFS answer is possible)."""
    ordered = sorted(frames, key=lambda f: (f["ts"] or 0.0, f["i"]))
    out = []
    last_data = None
    last_pos = None
    for pos, fr in enumerate(ordered):
        if fr["type"] == 2 and fr["a2"] == peer and fr["a1"] == mac:
            last_data = fr
            last_pos = pos
        ba = fr.get("ba")
        if not ba:
            continue
        sender = fr["a2"]
        if sender is None:
            sender = mac if fr["a1"] == peer else None
        if fr["a1"] != peer or sender != mac:
            continue
        gap = None
        adjacent = False
        if last_data is not None:
            gap = (fr["ts"] - last_data["ts"]) * 1e6
            adjacent = (last_pos == pos - 1)
        out.append((fr, last_data, gap, adjacent))
    return out


def rate_histogram(frames, src, dst):
    hist = Counter()
    for fr in direction_frames(frames, src, dst):
        if fr["type"] != 2:
            continue
        hist[fr["rate"]] += 1
    return hist


def dominant(hist):
    if not hist:
        return "none"
    label, count = hist.most_common(1)[0]
    total = sum(hist.values())
    return "%s (%d/%d frames, %.0f%%)" % (label, count, total, 100.0 * count / total)


def short_list(items, cap=LIST_CAP):
    out = list(items)
    if len(out) <= cap:
        return out
    return out[:cap] + ["... (%d more)" % (len(out) - cap)]


def print_direction(frames, title, src, dst, other_label):
    data = direction_frames(frames, src, dst)
    lines = []
    lines.append("-- %s" % title)
    if not data and not any(fr["a1"] == dst and fr["type"] == 1 for fr in frames):
        lines.append("   no frames")
    reqs = [fr for fr in data if fr["addba"] and fr["addba"]["kind"] == "req"]
    resps = [fr for fr in data if fr["addba"] and fr["addba"]["kind"] == "resp"]
    delbas = [fr for fr in data if fr["delba"]]
    bars = [fr for fr in data if fr["bar"]]
    bas = [fr for fr in data if fr["ba"]]
    qdata = [fr for fr in data if fr["type"] == 2]
    mgmt = [fr for fr in data if fr["type"] == 0]
    lines.append("   frames: %d data, %d management, %d control"
                 % (len(qdata), len(mgmt), len(bars) + len(bas)))

    if reqs:
        parts = []
        for fr in reqs:
            a = fr["addba"]
            parts.append("dialog %d tid %d buf %d%s%s ssn %d"
                         % (a["dialog"], a["tid"], a["buf"],
                            " A-MSDU" if a["amsdu"] else "",
                            "" if a["policy"] else " no-ack-policy",
                            a["ssn"]))
        lines.append("   ADDBA request: %d" % len(reqs))
        for text in short_list(parts):
            lines.append("     %s" % text)
    else:
        lines.append("   ADDBA request: 0")

    if resps:
        statuses = Counter(r["addba"]["status"] for r in resps)
        lines.append("   ADDBA response: %d (status %s)"
                     % (len(resps), ", ".join("%d x%d" % (s, c)
                                              for s, c in sorted(statuses.items()))))
        parts = []
        for fr in resps:
            a = fr["addba"]
            parts.append("dialog %d status %d tid %d buf %d%s"
                         % (a["dialog"], a["status"], a["tid"], a["buf"],
                            " A-MSDU" if a["amsdu"] else ""))
        for text in short_list(parts):
            lines.append("     %s" % text)
    else:
        lines.append("   ADDBA response: 0")

    if delbas:
        parts = []
        for fr in delbas:
            d = fr["delba"]
            parts.append("initiator=%s tid %d reason %d"
                         % (other_label if d["initiator"] else src,
                            d["tid"], d["reason"]))
        lines.append("   DELBA: %d" % len(delbas))
        for text in short_list(parts):
            lines.append("     %s" % text)
    else:
        lines.append("   DELBA: 0")

    if bars:
        parts = []
        for fr in bars:
            b = fr["bar"]
            parts.append("tid %d ssn %d%s"
                         % (b["tid"], b["ssn"],
                            " multi-tid" if b["multi_tid"] else ""))
        lines.append("   BAR (subtype 8): %d" % len(bars))
        for text in short_list(parts):
            lines.append("     %s" % text)
    else:
        lines.append("   BAR (subtype 8): 0")

    if bas:
        gaps = ba_gaps(frames, src, dst)
        by_frame = {id(fr): (prev, gap, adjacent)
                    for fr, prev, gap, adjacent in gaps}
        parts = []
        for fr in bas:
            b = fr["ba"]
            prev, gap, adjacent = by_frame.get(id(fr), (None, None, False))
            expect = frame_airtime_us(prev) if prev else None
            bits = 0
            for byte in b["bitmap"]:
                bits += bin(byte).count("1")
            text = ("tid %d ssn %d bitmap %d bits%s%s"
                    % (b["tid"], b["ssn"], b["bitmap_len"],
                       "" if b["compressed"] else " (uncompressed)",
                       "" if b["bitmap_ok"] else " TRUNCATED"))
            if adjacent and gap is not None:
                text += " gap %.1f us" % gap
                if expect:
                    text += " (SIFS+airtime %.1f us, delta %+.1f)" % (
                        SIFS_US + expect, gap - (SIFS_US + expect))
            elif gap is not None:
                text += (" gap %.1f us to the last peer data frame, not the "
                         "record before it" % gap)
            text += " bitmap_set %d" % bits
            parts.append(text)
        lines.append("   BlockAck (subtype 9): %d" % len(bas))
        for text in short_list(parts):
            lines.append("     %s" % text)
    else:
        lines.append("   BlockAck (subtype 9): 0")

    if qdata:
        qos = sum(1 for fr in qdata if fr["qos"])
        amsdu = sum(1 for fr in qdata if fr["amsdu"])
        retry = sum(1 for fr in qdata if fr["retry"])
        explicit, bursts, sizes, inferred = burst_report(frames, src, dst)
        lines.append("   data: %d (%d QoS, %d non-QoS, A-MSDU %d, retry %d)"
                     % (len(qdata), qos, len(qdata) - qos, amsdu, retry))
        if not qos:
            lines.append("   A-MPDU membership: not applicable, A-MPDU needs "
                         "QoS data frames")
        else:
            dist = ", ".join("%d MPDU%s: %d"
                             % (size, "" if size == 1 else "s", count)
                             for size, count in sorted(sizes.items()))
            lines.append("   A-MPDU membership: %s, %d bursts, %s"
                         % ("radiotap A-MPDU status" if explicit
                            else "inferred from consecutive records (no A-MPDU "
                                 "status field in this radiotap)",
                            len(bursts), dist or "no bursts"))
            multi = sum(1 for b in bursts if len(b["frames"]) > 1)
            lines.append("   bursts with more than one MPDU: %d, largest %d"
                         % (multi, max((len(b["frames"]) for b in bursts),
                                       default=0)))
        tid_hist = Counter(fr["tid"] for fr in qdata)
        lines.append("   TIDs: %s" % ", ".join("%s x%d" % (t, c)
                                               for t, c in sorted(tid_hist.items(),
                                                                  key=lambda kv: (kv[0] is None, kv[0]))))
        hist = rate_histogram(frames, src, dst)
        lines.append("   PHY rate histogram:")
        for label, count in hist.most_common():
            lines.append("     %-28s %d" % (label, count))
        lines.append("   dominant rate: %s" % dominant(hist))
    else:
        lines.append("   data: 0")
    return lines


def summarize_bursts(frames, src, dst):
    data = direction_frames(frames, src, dst)
    if data and not any(fr["qos"] for fr in data):
        return {"explicit": False, "bursts": 0, "sizes": Counter(),
                "inferred": True, "na": True, "multi": 0, "largest": 0}
    explicit, bursts, sizes, inferred = burst_report(frames, src, dst)
    return {
        "explicit": explicit,
        "bursts": len(bursts),
        "sizes": sizes,
        "inferred": inferred,
        "multi": sum(1 for b in bursts if len(b["frames"]) > 1),
        "largest": max((len(b["frames"]) for b in bursts), default=0),
        "na": False,
    }


def ba_gap_summary(frames, mac, peer):
    gaps = [gap for _fr, _prev, gap, adjacent in ba_gaps(frames, mac, peer)
            if adjacent and gap is not None]
    total = len(ba_gaps(frames, mac, peer))
    if not gaps:
        return None, total
    return sorted(gaps), total


def report(path, frames, meta, stats, mac, peer, note):
    lines = []
    lines.append("capture: %s" % path)
    lines.append("format: %s, link type %s, %d records, %d 802.11 frames%s"
                 % (meta["format"], meta.get("linktype"), meta.get("records", 0),
                    len(frames), " (file truncated)" if meta.get("cut") else ""))
    if meta.get("unit"):
        lines.append("timestamps: %s resolution" % meta["unit"])
    if frames:
        span = frames[-1]["ts"] - frames[0]["ts"]
        lines.append("time span: %.3f s" % span)
    names = Counter()
    problems = Counter()
    for fr in frames:
        for name in fr["rt_names"]:
            names[name] += 1
        if fr["rt_problem"]:
            problems[fr["rt_problem"]] += 1
    lines.append("radiotap fields: %s" % (", ".join(sorted(names)) or "none"))
    if problems:
        lines.append("radiotap notes: %s"
                     % ", ".join("%s x%d" % (p, c) for p, c in problems.most_common()))
    missing = [name for name in ("mcs", "vht", "ampdu") if name not in names]
    if missing:
        lines.append("radiotap never carries: %s (absent fields limit the "
                     "rate and A-MPDU detail)" % ", ".join(missing))
    types = Counter(fr["type"] for fr in frames)
    lines.append("frame mix: data %d, management %d, control %d"
                 % (types.get(2, 0), types.get(0, 0), types.get(1, 0)))
    if stats:
        extra = ", ".join("%s %d" % (k, v) for k, v in sorted(stats.items())
                          if k != "frames")
        if extra:
            lines.append("skipped: %s" % extra)
    lines.append("")
    lines.append("addresses: mac = %s, peer = %s" % (mac, peer))
    lines.append("selection: %s" % note["note"])
    if "pairs" in note:
        top = note["pairs"].most_common(4)
        lines.append("busiest unicast pairs: %s"
                     % "; ".join("%s -> %s %d" % (a, b, c) for (a, b), c in top))
    lines.append("")

    lines.extend(print_direction(frames, "peer -> mac (%s -> %s)" % (peer, mac),
                                 peer, mac, "peer"))
    lines.append("")
    lines.extend(print_direction(frames, "mac -> peer (%s -> %s)" % (mac, peer),
                                 mac, peer, "mac"))
    lines.append("")

    ack_peer = ack_count(frames, peer)
    ack_mac = ack_count(frames, mac)
    data_peer = sum(1 for fr in direction_frames(frames, peer, mac)
                    if fr["type"] == 2)
    data_mac = sum(1 for fr in direction_frames(frames, mac, peer)
                   if fr["type"] == 2)
    lines.append("-- acknowledgements")
    lines.append("   plain ACK with RA = peer: %d (Mac acknowledging %d data "
                 "frames from the peer)" % (ack_peer, data_peer))
    lines.append("   plain ACK with RA = mac: %d (peer acknowledging %d data "
                 "frames from the Mac)" % (ack_mac, data_mac))
    ba_to_peer = [fr for fr in frames
                  if fr.get("ba") and fr["a1"] == peer and fr["a2"] in (mac, None)]
    if data_peer:
        lines.append("   BlockAck per data frame from the peer: %d/%d"
                     % (len(ba_to_peer), data_peer))
    lines.append("")

    bursts_peer = summarize_bursts(frames, peer, mac)
    bursts_mac = summarize_bursts(frames, mac, peer)
    gap_sorted, gap_n = ba_gap_summary(frames, mac, peer)
    if gap_sorted:
        median = gap_sorted[len(gap_sorted) // 2]
        lines.append("BlockAck timing: %d BlockAck from the Mac, %d directly "
                     "after a peer MPDU, gap min %.1f / median %.1f / max %.1f us"
                     % (gap_n, len(gap_sorted), gap_sorted[0], median,
                        gap_sorted[-1]))
    elif gap_n:
        lines.append("BlockAck timing: %d BlockAck from the Mac, none directly "
                     "after a peer MPDU" % gap_n)
    else:
        lines.append("BlockAck timing: no BlockAck from the Mac")

    hw_ba = len(ba_to_peer)
    iphone_agg = bursts_peer["multi"] > 0
    rate_peer = dominant(rate_histogram(frames, peer, mac))
    rate_mac = dominant(rate_histogram(frames, mac, peer))
    lines.append("")
    lines.append("VERDICT: hardware BlockAck=%s  iphone_aggregating=%s  "
                 "rate peer->mac=%s  rate mac->peer=%s"
                 % ("yes (%d)" % hw_ba if hw_ba else "no",
                    "yes (%d bursts, largest %d)" % (bursts_peer["multi"],
                                                     bursts_peer["largest"])
                    if iphone_agg else
                    "no (%d data frames, %d ACKs to the peer, %d bursts)"
                    % (data_peer, ack_peer, bursts_peer["bursts"]),
                    rate_peer, rate_mac))
    return "\n".join(lines)


# --------------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Block Ack and rate report for a monitor capture "
                    "(radiotap, link type 127).")
    parser.add_argument("capture", help="pcap or pcapng file")
    parser.add_argument("--mac", help="MAC address of the Mac's AWDL interface")
    parser.add_argument("--peer", help="MAC address of the iPhone")
    args = parser.parse_args(argv)

    want_mac = norm_mac(args.mac) if args.mac else None
    want_peer = norm_mac(args.peer) if args.peer else None

    records, meta = load_capture(args.capture)
    frames, stats = build_frames(records)
    if not frames:
        raise SystemExit("no 802.11 frames could be decoded from %s"
                         % args.capture)
    frames.sort(key=lambda fr: (fr["ts"] or 0.0, fr["i"]))
    mac, peer, note = pick_addresses(frames, want_mac, want_peer)
    print(report(args.capture, frames, meta, stats, mac, peer, note))
    return 0


if __name__ == "__main__":
    sys.exit(main())
