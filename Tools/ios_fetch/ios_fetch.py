#!/usr/bin/env python3
"""Download and decrypt the iOS system image and/or the SystemOS cryptex from an IPSW URL,
without downloading the whole IPSW.

  python3 -I ios_fetch.py <ipsw-url> <outdir> [os|cryptex|both]

  os       the system volume: /System/Library/DriverExtensions (AppleBCMWLAN.dext and others)
  cryptex  the SystemOS cryptex: System/DriverKit/System/Library/dyld/dyld_shared_cache_arm64e
           (IO80211DriverKit lives there)

Each image is fetched with HTTP range requests (the IPSW stores it uncompressed), its AEA key is
unwrapped (the public key at com.apple.wkms.fcs-key-url opens wrapped-key with HPKE P-256 /
HKDF-SHA256 / AES-256-GCM, done by hpke_open.swift with CryptoKit, macOS 14+), and `aea decrypt`
writes a plain .dmg. Mount it with: hdiutil attach -readonly -nobrowse <file>.dmg

IPSW URLs per device: https://api.ipsw.me/v4/device/<identifier>?type=ipsw
"""
import json, os, plistlib, struct, subprocess, sys, urllib.request, zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def rng(url, a, b):
    r = urllib.request.Request(url, headers={"Range": "bytes=%d-%d" % (a, b)})
    with urllib.request.urlopen(r, timeout=120) as f:
        return f.read(), f.headers.get("Content-Range")


def zip_entries(url):
    _, cr = rng(url, 0, 0)
    size = int(cr.split("/")[1])
    t, _ = rng(url, size - 65536 - 22, size - 1)
    i = t.rfind(b"PK\x05\x06")
    cd_size, cd_off = struct.unpack_from("<II", t, i + 12)
    if cd_off == 0xFFFFFFFF:
        j = t.rfind(b"PK\x06\x06")
        cd_size, cd_off = struct.unpack_from("<QQ", t, j + 40)
    cd, _ = rng(url, cd_off, cd_off + cd_size - 1)
    out, p = {}, 0
    while p < len(cd) and cd[p:p + 4] == b"PK\x01\x02":
        meth, = struct.unpack_from("<H", cd, p + 10)
        csz, usz = struct.unpack_from("<II", cd, p + 20)
        nl, el, cl = struct.unpack_from("<HHH", cd, p + 28)
        lho, = struct.unpack_from("<I", cd, p + 42)
        name = cd[p + 46:p + 46 + nl].decode()
        ex, q = cd[p + 46 + nl:p + 46 + nl + el], 0
        while q < len(ex):
            hid, hl = struct.unpack_from("<HH", ex, q)
            if hid == 1:
                vals, k = list(struct.unpack_from("<%dQ" % (hl // 8), ex, q + 4)), 0
                if usz == 0xFFFFFFFF: usz = vals[k]; k += 1
                if csz == 0xFFFFFFFF: csz = vals[k]; k += 1
                if lho == 0xFFFFFFFF: lho = vals[k]; k += 1
            q += 4 + hl
        out[name] = (lho, csz, meth)
        p += 46 + nl + el + cl
    return out


def data_range(url, ent):
    lho, csz, meth = ent
    h, _ = rng(url, lho, lho + 29)
    nl, el = struct.unpack_from("<HH", h, 26)
    start = lho + 30 + nl + el
    return start, start + csz - 1, meth


def fetch_small(url, ent):
    a, b, meth = data_range(url, ent)
    d, _ = rng(url, a, b)
    return zlib.decompressobj(-15).decompress(d) if meth == 8 else d


def auth_data(path):
    with open(path, "rb") as f:
        d = f.read(1 << 20)
    assert d[:4] == b"AEA1", "not an AEA file"
    n, = struct.unpack_from("<I", d, 8)
    a, p, kv = d[12:12 + n], 0, {}
    while p < len(a):
        l, = struct.unpack_from("<I", a, p)
        k, v = a[p + 4:p + l].split(b"\0", 1)
        kv[k.decode()] = v.decode("latin1")
        p += l
    return kv


def hpke_tool(outdir):
    exe = os.path.join(outdir, "hpke_open")
    if not os.path.exists(exe):
        subprocess.run(["swiftc", "-O", os.path.join(HERE, "hpke_open.swift"), "-o", exe], check=True)
    return exe


def get_image(url, ents, name, outdir, tag):
    aea = os.path.join(outdir, tag + ".dmg.aea")
    dmg = os.path.join(outdir, tag + ".dmg")
    if os.path.exists(dmg):
        print(dmg, "already there"); return
    a, b, meth = data_range(url, ents[name])
    assert meth == 0, "image is compressed in the zip; unexpected"
    print("%s: %s, %.2f GB" % (tag, name, (b - a + 1) / 1e9), flush=True)
    subprocess.run(["curl", "-s", "-f", "-r", "%d-%d" % (a, b), url, "-o", aea], check=True)
    kv = auth_data(aea)
    pem = os.path.join(outdir, tag + "_key.pem")
    resp = os.path.join(outdir, tag + "_fcs.json")
    subprocess.run(["curl", "-s", "-f", "-L", kv["com.apple.wkms.fcs-key-url"], "-o", pem], check=True)
    with open(resp, "w") as f:
        f.write(kv["com.apple.wkms.fcs-response"])
    key = subprocess.run([hpke_tool(outdir), pem, resp], check=True, capture_output=True, text=True).stdout.strip()
    subprocess.run(["aea", "decrypt", "-i", aea, "-o", dmg, "-key-value", "base64:" + key], check=True)
    os.remove(aea)
    print("decrypted:", dmg, "\n  mount: hdiutil attach -readonly -nobrowse " + dmg, flush=True)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    url, outdir = sys.argv[1], sys.argv[2]
    what = sys.argv[3] if len(sys.argv) > 3 else "both"
    os.makedirs(outdir, exist_ok=True)
    ents = zip_entries(url)
    man = plistlib.loads(fetch_small(url, ents["BuildManifest.plist"]))
    m = man["BuildIdentities"][0]["Manifest"]
    if what in ("os", "both"):
        get_image(url, ents, m["OS"]["Info"]["Path"], outdir, "os")
    if what in ("cryptex", "both"):
        get_image(url, ents, m["Cryptex1,SystemOS"]["Info"]["Path"], outdir, "cryptex_systemos")


if __name__ == "__main__":
    main()
