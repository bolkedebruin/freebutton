#!/usr/bin/env python3
"""Time a full repaint of every display of a FreeButton device.

POST /api/bench?n=N (signed) makes the LVGL task repaint each display N
times; GET /api/bench returns, per display, the average and best repaint
time (to the last band on the wire), the time spent waiting for DMA, the
bands flushed and the glyphs rasterized with their CPU time.

usage: bench.py http://192.168.1.51 [-n 5] [--json out.json] [--compare before.json]
Credentials: FB_ACCESS_KEY_ID / FB_SECRET_ACCESS_KEY (or FB_CREDENTIALS), as
the other tools; never printed.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fb_sigv4 import credentials, sign_headers  # noqa: E402


def call(method, url):
    akid, secret = credentials()
    headers = sign_headers(method, url, {}, b"", akid, secret) if akid else {}
    req = urllib.request.Request(url, data=b"" if method == "POST" else None, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def run(base, n, timeout):
    status, body = call("GET", f"{base}/api/bench")
    if status != 200:
        sys.exit(f"GET /api/bench: {status} {body[:200]!r}")
    seq = json.loads(body).get("seq", 0)
    status, body = call("POST", f"{base}/api/bench?n={n}")
    if status not in (200, 202):
        sys.exit(f"POST /api/bench: {status} {body[:200]!r}")
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(0.5)
        status, body = call("GET", f"{base}/api/bench")
        if status == 200:
            result = json.loads(body)
            if result.get("seq", 0) > seq:
                return result
    sys.exit("timed out waiting for the benchmark")


def table(result, before=None):
    old = {d["display"]: d for d in (before or {}).get("displays", [])}
    print(f"{'display':8} {'avg ms':>8} {'best ms':>8} {'dma wait':>9} {'bands':>6} {'glyphs':>7} {'glyph ms':>9} {'lookups':>8} {'lookup ms':>10}"
          + ("   vs before" if old else ""))
    for d in result["displays"]:
        line = (f"{d['display']:8} {d['avg_us'] / 1000:8.1f} {d['best_us'] / 1000:8.1f} {d['dma_wait_us'] / 1000:9.1f}"
                f" {d['bands']:6} {d['glyphs']:7} {d['glyph_us'] / 1000:9.1f}"
                f" {d.get('lookups', 0):8} {d.get('lookup_us', 0) / 1000:10.1f}")
        if d["display"] in old and old[d["display"]]["avg_us"]:
            line += f"   {100.0 * d['avg_us'] / old[d['display']]['avg_us'] - 100:+6.1f}%"
        print(line)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("device")
    ap.add_argument("-n", type=int, default=5, help="repaints per display (1..50)")
    ap.add_argument("--json", help="also write the result to this file")
    ap.add_argument("--compare", help="an earlier --json result to compare with")
    ap.add_argument("--timeout", type=float, default=60.0)
    args = ap.parse_args()
    base = args.device.rstrip("/")
    result = run(base, args.n, args.timeout)
    before = json.load(open(args.compare)) if args.compare else None
    table(result, before)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(result, f, indent=1)


if __name__ == "__main__":
    main()
