#!/usr/bin/env python3
"""Send one file to this Mac's AirDrop through awdlsim's utun bridge.

The Mac's sharingd listens on TCP 8770 over awdl0 with a self-signed TLS
certificate and asks for no client certificate; in Everyone mode it takes
/Discover, /Ask and /Upload from any sender, and the Ask still waits for the
user to accept (Docs/AWDL_REFERENCIAS_EXTERNAS.md §12.7). This client binds to
the node's link-local address on the utun interface, so its packets reach
awdl0 through the virtual AWDL peer. Standard library only.

    python3 -I Tools/awdlsim/airdrop_send.py --iface utun5 --src fe80::... \\
        --dst fe80::... [--discover-only] FILE

Copyright (c) 2026 DrogaBox - GPL-2.0
"""
import argparse
import gzip
import http.client
import os
import plistlib
import secrets
import socket
import ssl
import sys
import time

PORT = 8770
HEADERS = {
    "Content-Type": "application/octet-stream",
    "Connection": "keep-alive",
    "Accept": "*/*",
    "User-Agent": "AirDrop/1.0",
    "Accept-Language": "en-us",
    "Accept-Encoding": "br, gzip, deflate",
}
UTI = {
    ".txt": "public.plain-text",
    ".png": "public.png",
    ".jpg": "public.jpeg",
    ".jpeg": "public.jpeg",
    ".pdf": "com.adobe.pdf",
}


def cpio_odc(entries):
    """A cpio "odc" archive (magic 070707) of (name, mode, data) entries.

    sharingd unpacks with the BOM library, which refuses "newc" (070701) with
    "bad file format": ditto -x -z shows the same.
    """
    out = bytearray()
    ino = 1
    for name, mode, data in entries + [("TRAILER!!!", 0, b"")]:
        n = name.encode() + b"\0"
        out += b"070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o" % (
            0, ino, mode, 0, 0, 1, 0, int(time.time()), len(n), len(data))
        out += n + data
        ino += 1
    return bytes(out)


def cpio_names(archive):
    """The entry names of an odc archive, to check cpio_odc."""
    names, off = [], 0
    while True:
        if archive[off:off + 6] != b"070707":
            raise ValueError("bad magic at %d" % off)
        nlen = int(archive[off + 59:off + 65], 8)
        size = int(archive[off + 65:off + 76], 8)
        name = archive[off + 76:off + 76 + nlen - 1].decode()
        off += 76 + nlen + size
        if name == "TRAILER!!!":
            return names
        names.append(name)


def connect(args):
    scope = socket.if_nametoindex(args.iface)
    s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    s.settimeout(args.timeout)
    s.bind((args.src, 0, 0, scope))
    s.connect((args.dst, PORT, 0, scope))
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    tls = ctx.wrap_socket(s)
    print("[+] TLS %s with [%s%%%s]:%d" % (tls.version(), args.dst, args.iface, PORT))
    conn = http.client.HTTPConnection("%s.local" % args.name)
    conn.sock = tls
    return conn


def post(conn, path, body, extra=None):
    headers = dict(HEADERS)
    headers.update(extra or {})
    conn.request("POST", path, body=body, headers=headers)
    r = conn.getresponse()
    data = r.read()
    try:
        reply = plistlib.loads(data) if data else {}
    except Exception:
        reply = {"raw": data[:200]}
    print("[%s] %s -> %d %s" % (time.strftime("%H:%M:%S"), path, r.status, reply))
    return r.status, reply


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iface", required=True, help="awdlsim's utun interface")
    ap.add_argument("--src", required=True, help="the node's link-local address")
    ap.add_argument("--dst", required=True, help="awdl0's link-local address")
    ap.add_argument("--name", default="awdlsim", help="sender name shown on the Mac")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--discover-only", action="store_true")
    ap.add_argument("file", nargs="?")
    args = ap.parse_args()
    if not args.discover_only and not args.file:
        ap.error("a FILE is needed unless --discover-only")

    sender_id = secrets.token_hex(6)
    conn = connect(args)
    status, reply = post(conn, "/Discover", plistlib.dumps({}, fmt=plistlib.FMT_BINARY))
    conn.close()
    if status != 200 or args.discover_only:
        return 0 if status == 200 else 1

    name = os.path.basename(args.file)
    with open(args.file, "rb") as fp:
        data = fp.read()
    ask = {
        "SenderComputerName": args.name,
        "SenderModelName": "MacBookPro14,3",
        "SenderID": sender_id,
        "BundleID": "com.apple.finder",
        "ConvertMediaFormats": False,
        "Files": [{
            "FileName": name,
            "FileType": UTI.get(os.path.splitext(name)[1].lower(), "public.data"),
            "FileBomPath": os.path.join(".", name),
            "FileIsDirectory": False,
            "ConvertMediaFormats": False,
        }],
    }
    conn = connect(args)
    print("[+] /Ask sent: accept it on the Mac (it waits for the user)")
    status, _ = post(conn, "/Ask", plistlib.dumps(ask, fmt=plistlib.FMT_BINARY))
    if status != 200:
        return 1
    archive = gzip.compress(cpio_odc([("./" + name, 0o100644, data)]))
    # sharingd refuses an upload without both headers ("Missing totalBytes / contentType").
    status, _ = post(conn, "/Upload", archive, {"Content-Type": "application/x-cpio",
                                                "TotalBytes": str(len(archive))})
    conn.close()
    return 0 if status == 200 else 1


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        a = cpio_odc([("./a.txt", 0o100644, b"hello"), ("./b", 0o100644, b"")])
        assert cpio_names(a) == ["./a.txt", "./b"], cpio_names(a)
        assert plistlib.loads(plistlib.dumps({"Files": [{"FileName": "x"}]},
                                             fmt=plistlib.FMT_BINARY))["Files"][0]["FileName"] == "x"
        print("selftest ok")
        sys.exit(0)
    sys.exit(main())
