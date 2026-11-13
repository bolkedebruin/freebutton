#!/usr/bin/env python3
"""Screenshot every display of a FreeButton device as PNG.

POST /api/screenshot (signed) makes the device repaint each display once and
write what it flushes to LittleFS /screenshot-<main|miniN>.rgb565 (8-byte
header "FBSS", uint16 LE width, uint16 LE height, then RGB565 big-endian
pixels). This tool waits until those objects of bucket `app` have changed and
stopped changing, downloads them and writes <out>/<name>.png.

usage: screenshot.py http://192.168.1.51 [--out DIR] [--only main]
Credentials: FB_ACCESS_KEY_ID / FB_SECRET_ACCESS_KEY (or FB_CREDENTIALS), as
the other tools; never printed.
"""
import argparse
import os
import struct
import sys
import time
import urllib.error
import urllib.request
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fb_sigv4 import credentials, sign_headers  # noqa: E402


def call(method, url, body=b""):
    akid, secret = credentials()
    headers = sign_headers(method, url, {}, body, akid, secret) if akid else {}
    req = urllib.request.Request(url, data=body if method in ("POST", "PUT") else None, method=method,
                                 headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def list_shots(base):
    status, _, body = call("GET", f"{base}/app?list-type=2&prefix=screenshot-")
    if status != 200:
        sys.exit(f"listing bucket app failed: {status} {body[:200]!r}")
    import re
    keys = re.findall(rb"<Key>(screenshot-[^<]+\.rgb565)</Key>", body)
    etags = re.findall(rb"<ETag>([^<]*)</ETag>", body)
    sizes = re.findall(rb"<Size>(\d+)</Size>", body)
    return {k.decode(): (e.decode(), int(s)) for k, e, s in zip(keys, etags, sizes)}


def png(path, w, h, rgb565_be):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        row = rgb565_be[y * w * 2:(y + 1) * w * 2]
        for x in range(w):
            v = (row[2 * x] << 8) | row[2 * x + 1]
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            raw += bytes(((r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("device")
    ap.add_argument("--out", default=".")
    ap.add_argument("--only", help="only this display (main, mini0, ...)")
    ap.add_argument("--timeout", type=float, default=20.0)
    args = ap.parse_args()
    base = args.device.rstrip("/")
    os.makedirs(args.out, exist_ok=True)

    before = list_shots(base)
    status, _, body = call("POST", f"{base}/api/screenshot")
    if status != 202:
        sys.exit(f"POST /api/screenshot: {status} {body[:200]!r}")

    # The device writes the files one display after another (the main display
    # takes longest). Wait until every file of the previous run was rewritten,
    # then until the set stayed the same for one poll. Without a previous run
    # the number of displays is unknown: wait for the set to stay the same
    # for three polls.
    deadline = time.time() + args.timeout
    last = None
    stable = 0
    while time.time() < deadline:
        time.sleep(0.7)
        now = list_shots(base)
        changed = {k for k, v in now.items() if before.get(k) != v}
        stable = stable + 1 if now == last else 0
        last = now
        if before and changed >= set(before) and stable >= 1:
            break
        if not before and changed and stable >= 3:
            break
    else:
        sys.exit("timed out waiting for the screenshot files")

    for key in sorted(last):
        name = key[len("screenshot-"):-len(".rgb565")]
        if args.only and name != args.only:
            continue
        status, _, data = call("GET", f"{base}/app/{key}")
        if status != 200 or data[:4] != b"FBSS":
            print(f"{key}: bad download ({status})", file=sys.stderr)
            continue
        w, h = struct.unpack("<HH", data[4:8])
        pix = data[8:]
        if len(pix) < w * h * 2:
            print(f"{key}: short ({len(pix)} of {w * h * 2} bytes)", file=sys.stderr)
            pix = pix + bytes(w * h * 2 - len(pix))
        out = os.path.join(args.out, f"{name}.png")
        png(out, w, h, pix)
        print(out)


if __name__ == "__main__":
    main()
