#!/usr/bin/env python3
"""An AirDrop receiver behind awdlsim's utun bridge, for the Mac as sender.

With `awdlsim --airdrop --tun` the virtual AWDL peer announces
`<id>._airdrop._tcp.local` on port 8770 in its action frames, as an iPhone
does. This script is that receiver: it listens on the node's link-local
address on the utun interface with a self-signed TLS certificate and answers
/Discover (so the node shows in the Mac's AirDrop sheet), /Ask (accepts) and
/Upload (counts the bytes, keeps nothing unless --save). It also answers the
mDNS questions the Mac sends the node for the service's TXT record, SRV target
and address before it connects. Each request is printed with its time, so a
run shows how far the Mac got.
Standard library and /usr/bin/openssl only.

    python3 -I Tools/awdlsim/airdrop_recv.py --iface utun4 --addr fe80::a5:3eff:fe00:1

Copyright (c) 2026 DrogaBox - GPL-2.0
"""
import argparse
import http.server
import os
import plistlib
import select
import socket
import socketserver
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time

PORT = 8770
START = time.time()


def log(msg):
    print("[%8.3f] %s" % (time.time() - START, msg), flush=True)


def make_cert(directory, name):
    key = os.path.join(directory, "key.pem")
    cert = os.path.join(directory, "cert.pem")
    subprocess.run(
        ["/usr/bin/openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key,
         "-out", cert, "-days", "2", "-subj", "/CN=" + name],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return cert, key


MDNS_PORT = 5353
MDNS_GROUP = "ff02::fb"
T_PTR, T_TXT, T_AAAA, T_SRV, T_ANY = 12, 16, 28, 33, 255


def dns_name(name):
    out = b""
    for part in name.rstrip(".").split("."):
        out += bytes([len(part)]) + part.encode()
    return out + b"\0"


def dns_read_name(msg, off):
    """A possibly compressed name at off: (dotted lower-case name, offset after it)."""
    parts, end, hops = [], None, 0
    while off < len(msg):
        n = msg[off]
        if n == 0:
            off += 1
            break
        if n & 0xC0 == 0xC0:
            if off + 1 >= len(msg) or hops > 16:
                break
            if end is None:
                end = off + 2
            off = ((n & 0x3F) << 8) | msg[off + 1]
            hops += 1
            continue
        parts.append(msg[off + 1:off + 1 + n].decode("utf-8", "replace"))
        off += 1 + n
    return ".".join(parts).lower(), (end if end is not None else off)


class Mdns(threading.Thread):
    """Answers the Mac's mDNS questions about the receiver. The service is
    found from the node's action frames; the Mac then asks the peer itself for
    the TXT record, the SRV target and its address before it connects."""

    def __init__(self, iface, addr, instance, host, port, flags):
        super().__init__(daemon=True)
        self.scope = socket.if_nametoindex(iface)
        self.addr = addr
        self.service = "_airdrop._tcp.local"
        self.instance = "%s.%s" % (instance, self.service)
        self.host = "%s.local" % host
        self.flags = flags
        self.port = port
        self.multi = self.sock(("::", MDNS_PORT, 0, 0))
        group = socket.inet_pton(socket.AF_INET6, MDNS_GROUP) + struct.pack("@I", self.scope)
        self.multi.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, group)
        self.uni = self.sock((addr, MDNS_PORT, 0, self.scope))
        self.uni.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF, self.scope)
        self.uni.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 255)
        self.uni.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_UNICAST_HOPS, 255)

    @staticmethod
    def sock(bind):
        s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        s.bind(bind)
        return s

    def records(self):
        def rr(name, kind, cls, ttl, data):
            return dns_name(name) + struct.pack(">HHIH", kind, cls, ttl, len(data)) + data
        txt = b"flags=%d" % self.flags
        return [
            rr(self.service, T_PTR, 1, 4500, dns_name(self.instance)),
            rr(self.instance, T_SRV, 0x8001, 120,
               struct.pack(">HHH", 0, 0, self.port) + dns_name(self.host)),
            rr(self.instance, T_TXT, 0x8001, 4500, bytes([len(txt)]) + txt),
            rr(self.host, T_AAAA, 0x8001, 120, socket.inet_pton(socket.AF_INET6, self.addr)),
        ]

    def response(self, ident=0):
        rrs = self.records()
        return struct.pack(">HHHHHH", ident, 0x8400, 0, len(rrs), 0, 0) + b"".join(rrs)

    def run(self):
        ours = {self.service, self.instance, self.host}
        while True:
            ready, _, _ = select.select([self.multi, self.uni], [], [])
            for s in ready:
                try:
                    msg, src = s.recvfrom(9000)
                except OSError:
                    continue
                if len(msg) < 12 or src[3] != self.scope:
                    continue
                ident, flags, nq = struct.unpack(">HHH", msg[:6])
                if flags & 0x8000:
                    continue
                off, asked, unicast = 12, [], src[1] != MDNS_PORT
                for _ in range(nq):
                    name, off = dns_read_name(msg, off)
                    if off + 4 > len(msg):
                        break
                    kind, cls = struct.unpack(">HH", msg[off:off + 4])
                    off += 4
                    unicast |= bool(cls & 0x8000)
                    asked.append((name, kind))
                mine = [q for q in asked if q[0] in ours]
                log("mDNS %s from %s: %s%s" % (
                    "query" if s is self.multi else "unicast query", src[0].split("%")[0],
                    ", ".join("%s type %d" % q for q in asked) or "no question",
                    "" if mine else " (not ours)"))
                if not mine:
                    continue
                try:
                    self.uni.sendto(self.response(), (MDNS_GROUP, MDNS_PORT, 0, self.scope))
                    if unicast:
                        self.uni.sendto(self.response(ident), src)
                except OSError as e:
                    log("mDNS answer failed: %s" % e)


class Tap(threading.Thread):
    """With --pairing: listens on the ports of awdlsim's Rapport records
    (`_applicationservicepairing` 56962, `_appsvcprepair` 56963) and logs who
    connects and what comes first. There is no Rapport pairing behind it: it
    only shows how far the Mac gets on its own."""

    def __init__(self, addr, scope, port, what):
        super().__init__(daemon=True)
        self.port, self.what = port, what
        self.s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind((addr, port, 0, scope))
        self.s.listen(8)

    def run(self):
        while True:
            c, src = self.s.accept()
            threading.Thread(target=self.serve, args=(c, src), daemon=True).start()

    def serve(self, c, src):
        log("%s: TCP from %s to port %d" % (self.what, src[0].split("%")[0], self.port))
        c.settimeout(30)
        total, first = 0, b""
        try:
            while True:
                b = c.recv(65536)
                if not b:
                    break
                if not first:
                    first = b
                    kind = "TLS" if b[:2] == b"\x16\x03" else "HTTP" if b[:4] in (b"GET ", b"POST", b"PUT ") else "?"
                    log("%s: first %d bytes (%s): %s" % (self.what, len(b), kind, b[:96].hex()))
                total += len(b)
        except OSError as e:
            log("%s: %s" % (self.what, e))
        finally:
            c.close()
        log("%s: connection closed after %d bytes" % (self.what, total))


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "AirDrop/1.0"

    def setup(self):
        peer = self.client_address[0]
        log("TCP from %s" % peer)
        try:
            self.request = self.server.tls.wrap_socket(self.request, server_side=True)
        except (ssl.SSLError, OSError) as e:
            log("TLS handshake failed: %s" % e)
            raise
        log("TLS %s %s" % (self.request.version(), self.request.cipher()[0]))
        super().setup()

    def log_message(self, fmt, *args):
        pass

    def body(self):
        """The request body: Content-Length or chunked. Returns (bytes kept, total length)."""
        keep = self.path != "/Upload" or self.server.save
        chunks, total = [], 0
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            while True:
                line = self.rfile.readline()
                if not line:
                    break
                size = int(line.split(b";")[0].strip() or b"0", 16)
                if size == 0:
                    while self.rfile.readline() not in (b"\r\n", b"\n", b""):
                        pass
                    break
                left = size
                while left:
                    d = self.rfile.read(min(left, 65536))
                    if not d:
                        return b"".join(chunks), total
                    left -= len(d)
                    total += len(d)
                    if keep:
                        chunks.append(d)
                    if self.path == "/Upload" and total // 1000000 != (total - len(d)) // 1000000:
                        log("  upload at %d MB" % (total // 1000000))
                self.rfile.readline()
        else:
            left = int(self.headers.get("Content-Length", "0"))
            while left:
                d = self.rfile.read(min(left, 65536))
                if not d:
                    break
                left -= len(d)
                total += len(d)
                if keep:
                    chunks.append(d)
        return b"".join(chunks), total

    def reply(self, obj=None):
        data = plistlib.dumps(obj, fmt=plistlib.FMT_BINARY) if obj is not None else b""
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        t0 = time.time()
        if self.path == "/Upload":
            log("POST /Upload started (%s)" % (
                self.headers.get("Content-Length") or self.headers.get("Transfer-Encoding") or "no length"))
        data, total = self.body()
        took = time.time() - t0
        keys = ""
        if data and self.path != "/Upload":
            try:
                keys = " keys " + ",".join(sorted(plistlib.loads(data).keys()))
            except Exception:
                keys = " (body is not a plist)"
        if self.path == "/Discover":
            log("POST /Discover %d bytes%s -> 200" % (total, keys))
            self.reply({
                "ReceiverComputerName": self.server.name,
                "ReceiverModelName": "iPhone14,5",
                "ReceiverMediaCapabilities": b'{"Version":1}',
            })
        elif self.path == "/Hello":
            try:
                shown = repr(plistlib.loads(data))[:600]
            except Exception:
                shown = data[:200].hex()
            log("POST /Hello %d bytes %s -> 200" % (total, shown))
            self.reply(self.server.hello)
        elif self.path == "/Ask":
            log("POST /Ask %d bytes%s -> 200 (accepted)" % (total, keys))
            self.reply({"ReceiverComputerName": self.server.name, "ReceiverModelName": "iPhone14,5"})
        elif self.path == "/Upload":
            rate = total / took / 1000 if took > 0 else 0
            log("POST /Upload %d bytes in %.2f s, %.0f kB/s -> 200" % (total, took, rate))
            if self.server.save and data:
                with open(self.server.save, "wb") as f:
                    f.write(data)
                log("saved the archive to %s" % self.server.save)
            self.reply()
        else:
            log("POST %s %d bytes -> 404" % (self.path, total))
            self.send_error(404)

    def do_GET(self):
        log("GET %s -> 404" % self.path)
        self.send_error(404)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    address_family = socket.AF_INET6
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        log("connection from %s ended: %s" % (client_address[0], sys.exc_info()[1]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iface", required=True, help="the utun interface awdlsim printed")
    ap.add_argument("--addr", required=True, help="the node's link-local address on it")
    ap.add_argument("--name", default="awdlsim", help="the name shown in the AirDrop sheet")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--save", help="write the uploaded archive (cpio, gzip) to this file")
    ap.add_argument("--instance", default="311dbc80cdc3", help="the service instance awdlsim announces")
    ap.add_argument("--host", default="awdlsim", help="the node's host name, without .local")
    ap.add_argument("--flags", type=int, default=46075,
                    help="the TXT flags of the mDNS answers; give the value of awdlsim --airdrop-flags")
    ap.add_argument("--pairing", action="store_true",
                    help="also log connections to the ports of awdlsim --pairing (56962, 56963)")
    a = ap.parse_args()

    with tempfile.TemporaryDirectory() as d:
        cert, key = make_cert(d, a.name)
        # Started before awdlsim, it waits for the utun and the node's address,
        # so the first connection the Mac makes after discovery is not refused.
        deadline = time.time() + 3600
        while True:
            try:
                scope = socket.if_nametoindex(a.iface)
                probe = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
                probe.bind((a.addr, 0, 0, scope))
                probe.close()
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.05)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(cert, key)
        tls.verify_mode = ssl.CERT_NONE
        srv = Server((a.addr, a.port, 0, scope), Handler)
        srv.tls, srv.name, srv.save = tls, a.name, a.save
        srv.hello = {}
        Mdns(a.iface, a.addr, a.instance, a.host, a.port, a.flags).start()
        if a.pairing:
            Tap(a.addr, scope, 56962, "pairing").start()
            Tap(a.addr, scope, 56963, "pre-pairing").start()
        log("receiver '%s' on [%s%%%s]:%d, waiting for the Mac (Ctrl-C ends)" %
            (a.name, a.addr, a.iface, a.port))
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            log("stopped")


if __name__ == "__main__":
    main()
