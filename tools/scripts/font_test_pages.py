#!/usr/bin/env python3
"""Font test pages: every UI font size on the main display, screenshotted.

Through the device's JS REPL (WebSocket /ws/repl, presigned) this creates
display items on spare pages (runtime only: nothing is saved to config.json),
one page per font size (fontsize 1..5 = 14, 24, 32, 48, 72 px), fills them
with text that exercises descenders, accents, digits, symbols and Material
Design Icons, shows each page, takes a screenshot (POST /api/screenshot, see
screenshot.py) and samples the LVGL pool (/api/device lvglFree). Afterwards
it removes the items and shows the page that was shown before.

usage: font_test_pages.py http://192.168.1.51 [--out DIR] [--keep]
Credentials: FB_ACCESS_KEY_ID / FB_SECRET_ACCESS_KEY, never printed.
Uses Python's standard library only (a minimal WebSocket client).
"""
import argparse
import base64
import json
import os
import socket
import struct
import subprocess
import sys
import time
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fb_sigv4 import credentials, presign_url  # noqa: E402

FIRST_PAGE = 11
PARK_PAGE = 99

# (fontsize, [(y %, caption, value), ...]). x 0, width 100 %, box type 1.
ICONS = "\\u{F0599}\\u{F0590}\\u{F0597}\\u{F0335}\\u{F0D43}\\u{F0536}\\u{F075D}\\u{F1C56}"
PAGES = [
    (1, [(0, "14 px text", "Aggjpqy ÀÉÇÅÖÜß ñ €£$ 23,5°C"),
         (25, "14 px digits", "0123456789 ±×÷≠≤≥√∞ ↑↓"),
         (50, "14 px icons", ICONS + " \\u{F04AE}\\u{F040E}\\u{F04AD}\\u{F03E4}\\u{F04DB} \\u{F012C}\\u{F0156}"),
         (75, "14 px mixed", "\\u{F0599} Zonnig, 18° \\u{F075D} 45%")]),
    (2, [(0, "24 px", "Aggjpqy ÀÉÇÅÖÜ"),
         (33, "24 px digits", "0123456789 °C %"),
         (66, "24 px icons", ICONS)]),
    (3, [(0, "32 px", "Ágjpqy ÉÇÜ"),
         (33, "32 px digits", "12:45 23,5°"),
         (66, "32 px icons", "\\u{F0599}\\u{F0590}\\u{F0335}\\u{F0D43}")]),
    (4, [(0, "48 px", "Ágjp 23,5°C"),
         (50, "48 px icons", "\\u{F0599} 1013")]),
    (5, [(0, "72 px", "Ég 21°"),
         (50, "72 px icons", "\\u{F0599}\\u{F0590}")]),
]


class WS:
    """Minimal RFC 6455 client: text frames, client masking, no extensions."""

    def __init__(self, url):
        u = urllib.parse.urlsplit(url)
        self.s = socket.create_connection((u.hostname, u.port or 80), timeout=15)
        key = base64.b64encode(os.urandom(16)).decode()
        path = u.path + ("?" + u.query if u.query else "")
        self.s.sendall((f"GET {path} HTTP/1.1\r\nHost: {u.hostname}\r\nUpgrade: websocket\r\n"
                        f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                        f"Sec-WebSocket-Version: 13\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.s.recv(1024)
            if not chunk:
                raise RuntimeError("connection closed during the handshake")
            head += chunk
        status = head.split(b"\r\n", 1)[0]
        if b" 101 " not in status:
            raise RuntimeError(f"WebSocket handshake failed: {status.decode(errors='replace')}")
        self.buf = head.split(b"\r\n\r\n", 1)[1]

    def _recv(self, n):
        while len(self.buf) < n:
            chunk = self.s.recv(4096)
            if not chunk:
                raise RuntimeError("connection closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send(self, text):
        data = text.encode()
        mask = os.urandom(4)
        n = len(data)
        hdr = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else
                               bytes([0x80 | 126]) + struct.pack(">H", n) if n < 65536 else
                               bytes([0x80 | 127]) + struct.pack(">Q", n))
        self.s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self):
        while True:
            b0, b1 = self._recv(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._recv(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._recv(8))[0]
            payload = self._recv(n)
            op = b0 & 0x0F
            if op == 0x1:
                return payload.decode(errors="replace")
            if op == 0x8:
                raise RuntimeError("closed by the device")
            # ping/pong/binary: ignore

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


class Repl:
    def __init__(self, base):
        akid, secret = credentials()
        url = base.replace("http://", "ws://") + "/ws/repl"
        if akid:
            url = presign_url(url.replace("ws://", "http://"), akid, secret).replace("http://", "ws://")
        self.ws = WS(url)
        self.seq = 0

    def eval(self, code):
        self.seq += 1
        qid = f"q{self.seq}"
        self.ws.send(json.dumps({"type": "eval", "id": qid, "code": code}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("type") == "result" and msg.get("id") == qid:
                if not msg.get("success"):
                    raise RuntimeError(f"{code[:80]!r}: {msg.get('error')}")
                return msg.get("value")


def js_str(s):
    # JS string literal; keep \\u{...} escapes for the REPL's parser.
    return '"' + s.replace('"', '\\"') + '"'


def lvgl_free(base):
    with urllib.request.urlopen(f"{base}/api/device", timeout=10) as r:
        d = json.load(r)
    return d.get("lvglFree"), d.get("heapFree")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("device")
    ap.add_argument("--out", default="font-test")
    ap.add_argument("--keep", action="store_true", help="leave the items and the last page shown")
    args = ap.parse_args()
    base = args.device.rstrip("/")
    os.makedirs(args.out, exist_ok=True)
    shot = os.path.join(os.path.dirname(os.path.abspath(__file__)), "screenshot.py")

    repl = Repl(base)
    shown = repl.eval("fb.main.display.getShownPage()")
    created = []
    # Items on page 0 show on every page: park them on an unused page while
    # the test runs (runtime only) so the test pages are drawn alone.
    parked = repl.eval("var r=[];var a=fb.main.display.getAll();"
                       "for(var i=0;i<a.length;i++){if(fb.main.display.get(a[i]).page===0)r.push(a[i]);}r.join(',')") or ""
    parked = [int(x) for x in str(parked).split(",") if x.strip()]
    try:
        for iid in parked:
            repl.eval(f"fb.main.display.setPage({iid}, {PARK_PAGE})")
        for i, (size, rows) in enumerate(PAGES):
            page = FIRST_PAGE + i
            for y, caption, value in rows:
                iid = repl.eval(f"fb.main.display.create(0, {y}, 100, 1, {size}, 0, {js_str(caption)}, \"\", 0, {page})")
                if not iid:
                    raise RuntimeError(f"create failed on page {page}")
                created.append(int(iid))
                repl.eval(f"fb.main.display.setValue({int(iid)}, {js_str(value)})")
        for i, (size, _) in enumerate(PAGES):
            page = FIRST_PAGE + i
            repl.eval(f"fb.main.display.showPage({page})")
            time.sleep(1.5)  # drawn, and one lvglFree sample (1 s period)
            free, heap = lvgl_free(base)
            out = subprocess.run([sys.executable, shot, base, "--out", args.out, "--only", "main"],
                                 capture_output=True, text=True)
            png = os.path.join(args.out, f"fontsize{size}.png")
            if out.returncode == 0 and out.stdout.strip():
                os.replace(out.stdout.strip().splitlines()[-1], png)
                print(f"page {page} fontsize {size}: lvglFree {free} B, heapFree {heap} B -> {png}")
            else:
                print(f"page {page} fontsize {size}: lvglFree {free} B; screenshot failed: {out.stderr.strip()}")
    finally:
        if not args.keep:
            for iid in created:
                try:
                    repl.eval(f"fb.main.display.remove({iid})")
                except Exception as e:  # noqa: BLE001
                    print(f"remove {iid}: {e}", file=sys.stderr)
        for iid in parked:
            try:
                repl.eval(f"fb.main.display.setPage({iid}, 0)")
            except Exception as e:  # noqa: BLE001
                print(f"restore page of {iid}: {e}", file=sys.stderr)
        if not args.keep:
            repl.eval(f"fb.main.display.showPage({int(shown or 0)})")
        repl.ws.close()


if __name__ == "__main__":
    main()
