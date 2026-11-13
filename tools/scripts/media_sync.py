#!/usr/bin/env python3
"""Sync a media bucket (default: fonts) between this repo and a device.

The bucket `fonts` is backed by the device's raw `media` partition
(docs/MEDIA-STORE.md), not by LittleFS: a LittleFS image (`uploadfs`,
tools/scripts/ota_fs.py) never contains or changes it. This tool talks to
the device's S3 API:

  push  upload media/<bucket>/* (or --dir) to the bucket. An object whose
        ETag equals the local file's MD5 is skipped.
  pull  download every object of the bucket into a directory.
  ls    list the objects (size, ETag) and the store usage the device reports
        on HeadBucket (x-fb-media-total / -used / -free / -reclaimable).

Credentials (needed for push; ls and pull of a public bucket work without):
FB_CREDENTIALS ("FBCRED:<akid>:<secret>" or "<akid>:<secret>"), or
FB_ACCESS_KEY_ID + FB_SECRET_ACCESS_KEY. They are shown on the device (hold
the factory button 6 s). They are never printed.

usage:
  python3 tools/scripts/media_sync.py ls   http://192.168.1.50
  python3 tools/scripts/media_sync.py push http://192.168.1.50            # media/fonts/
  python3 tools/scripts/media_sync.py push http://192.168.1.50 --dir devices/btn_x/backup/fs-.../fonts
  python3 tools/scripts/media_sync.py pull http://192.168.1.50 /tmp/fonts
"""
import argparse
import base64
import hashlib
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fb_sigv4  # noqa: E402

PROJECT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
S3NS = "{http://s3.amazonaws.com/doc/2006-03-01/}"
USAGE_HEADERS = ("x-fb-media-total", "x-fb-media-used", "x-fb-media-free", "x-fb-media-reclaimable")
# wear since the device booted: counts, not bytes (4 KB sectors; a page erase is 16)
WEAR_HEADERS = ("x-fb-media-erase-sectors", "x-fb-media-erase-page-max", "x-fb-media-dir-generation")


def _credentials():
    """(akid, secret) from the environment, or None. Never printed."""
    if os.environ.get("FB_CREDENTIALS") or (
            os.environ.get("FB_ACCESS_KEY_ID") and os.environ.get("FB_SECRET_ACCESS_KEY")):
        return fb_sigv4.credentials()
    return None


class Device:
    def __init__(self, base, creds):
        self.base = base.rstrip("/")
        self.creds = creds

    def request(self, method, path, body=None, headers=None, timeout=120):
        """(status, headers, body). Raises urllib.error.HTTPError on >= 400."""
        url = self.base + path
        headers = dict(headers or {})
        if self.creds:
            headers = fb_sigv4.sign_headers(method, url, headers, body or b"", *self.creds)
        req = urllib.request.Request(url, data=body, method=method, headers=headers)
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.headers, r.read()


def _key_path(bucket, key):
    """URL path of an object; every segment percent-encoded ('+' as %2B)."""
    return "/%s/%s" % (bucket, "/".join(urllib.parse.quote(s, safe="-_.~") for s in key.split("/")))


def _etag(value):
    return (value or "").strip().strip('"').lower()


def _fail(e, what):
    if isinstance(e, urllib.error.HTTPError):
        if e.code == 403 and e.headers.get("x-freebutton-auth"):
            sys.exit(f"{what}: the device requires a signature: set FB_CREDENTIALS or "
                     "FB_ACCESS_KEY_ID/FB_SECRET_ACCESS_KEY (hold the factory button 6 s to show them)")
        detail = e.read().decode(errors="replace")
        try:
            code = ET.fromstring(detail).findtext("Code") or ""
        except ET.ParseError:
            code = ""
        if e.code == 507:
            sys.exit(f"{what}: 507 Insufficient Storage: the media store is full (nothing was written)")
        sys.exit(f"{what}: HTTP {e.code} {code or detail[:200]}")
    sys.exit(f"{what}: {e}")


def list_objects(dev, bucket):
    """[(key, size, etag)] of the whole bucket (ListObjectsV2, paged)."""
    out, token = [], None
    while True:
        q = {"list-type": "2"}
        if token:
            q["continuation-token"] = token
        try:
            _, _, body = dev.request("GET", f"/{bucket}?{urllib.parse.urlencode(q, quote_via=urllib.parse.quote)}")
        except Exception as e:  # noqa: BLE001
            _fail(e, f"list {bucket}")
        root = ET.fromstring(body)
        for c in root.iter(f"{S3NS}Contents"):
            out.append((c.findtext(f"{S3NS}Key"), int(c.findtext(f"{S3NS}Size") or 0),
                        _etag(c.findtext(f"{S3NS}ETag"))))
        if root.findtext(f"{S3NS}IsTruncated") != "true":
            return out
        token = root.findtext(f"{S3NS}NextContinuationToken")
        if not token:
            return out


def head_bucket(dev, bucket):
    """The x-fb-media-* usage headers of HeadBucket ({} when absent)."""
    try:
        _, headers, _ = dev.request("HEAD", f"/{bucket}")
    except urllib.error.HTTPError as e:
        if e.code == 404:
            sys.exit(f"the device has no bucket '{bucket}'")
        _fail(e, f"HEAD {bucket}")
    except Exception as e:  # noqa: BLE001
        _fail(e, f"HEAD {bucket}")
    return {h: headers.get(h) for h in USAGE_HEADERS + WEAR_HEADERS if headers.get(h) is not None}


def _human(n):
    for unit in ("B", "KB", "MB"):
        if n < 1024 or unit == "MB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024.0


def _print_usage(usage):
    if not usage:
        print("  (no x-fb-media-* usage headers on HeadBucket)")
        return
    parts = []
    for h in USAGE_HEADERS:
        if h in usage:
            v = usage[h]
            parts.append(f"{h[len('x-fb-media-'):]} {_human(int(v)) if v.isdigit() else v}")
    print("  media store: " + ", ".join(parts))
    wear = [f"{h[len('x-fb-media-'):]} {usage[h]}" for h in WEAR_HEADERS if h in usage]
    if wear:
        print("  wear since boot: " + ", ".join(wear))


def local_files(directory):
    """{key: path} for every font file (.ttf/.otf) below directory: the bucket
    refuses other keys, so media/fonts/README.md and the license texts stay
    local (dotfiles skipped too)."""
    files = {}
    for root, dirs, names in os.walk(directory):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in sorted(names):
            if name.startswith(".") or not name.lower().endswith((".ttf", ".otf")):
                continue
            path = os.path.join(root, name)
            key = os.path.relpath(path, directory).replace(os.sep, "/")
            files[key] = path
    return files


def cmd_ls(dev, args):
    objs = list_objects(dev, args.bucket)
    for key, size, etag in objs:
        print(f"{size:>10}  {etag or '-':32}  {key}")
    print(f"{len(objs)} objects, {_human(sum(s for _, s, _ in objs))} in bucket {args.bucket}")
    _print_usage(head_bucket(dev, args.bucket))


def cmd_push(dev, args):
    if not dev.creds and not args.dry_run:
        sys.exit("push needs credentials: set FB_CREDENTIALS or FB_ACCESS_KEY_ID/FB_SECRET_ACCESS_KEY")
    directory = args.dir or os.path.join(PROJECT, "media", args.bucket)
    if not os.path.isdir(directory):
        sys.exit(f"no directory {directory}")
    files = local_files(directory)
    if not files:
        sys.exit(f"no files in {directory}")
    remote = {key: etag for key, _, etag in list_objects(dev, args.bucket)}
    sent = skipped = 0
    for key, path in files.items():
        with open(path, "rb") as f:
            body = f.read()
        md5 = hashlib.md5(body)
        if remote.get(key) == md5.hexdigest():
            print(f"  same  {key}")
            skipped += 1
            continue
        print(f"  {'would put' if args.dry_run else 'put'}  {key} ({len(body)} bytes)")
        if args.dry_run:
            sent += 1
            continue
        headers = {"Content-Type": "application/octet-stream",
                   "Content-MD5": base64.b64encode(md5.digest()).decode()}
        try:
            _, rh, _ = dev.request("PUT", _key_path(args.bucket, key), body=body, headers=headers, timeout=300)
        except Exception as e:  # noqa: BLE001
            _fail(e, f"PUT {key}")
        if rh.get("ETag") and _etag(rh.get("ETag")) != md5.hexdigest():
            sys.exit(f"PUT {key}: the device stored ETag {_etag(rh.get('ETag'))}, expected {md5.hexdigest()}")
        sent += 1
    print(f"[media_sync] {args.bucket}: {sent} {'to upload' if args.dry_run else 'uploaded'}, {skipped} unchanged")
    if not args.dry_run:
        _print_usage(head_bucket(dev, args.bucket))


def cmd_pull(dev, args):
    directory = os.path.abspath(args.dir)
    objs = list_objects(dev, args.bucket)
    for key, size, _etag_ in objs:
        parts = key.split("/")
        if not key or key.startswith("/") or any(p in ("", ".", "..") for p in parts):
            print(f"  skip unsafe key {key!r}", file=sys.stderr)
            continue
        try:
            _, _, body = dev.request("GET", _key_path(args.bucket, key), timeout=300)
        except Exception as e:  # noqa: BLE001
            _fail(e, f"GET {key}")
        dst = os.path.join(directory, *parts)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(body)
        print(f"  got  {key} ({len(body)} bytes)")
    print(f"[media_sync] {args.bucket}: {len(objs)} objects into {directory}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("ls", "push", "pull"):
        p = sub.add_parser(name)
        p.add_argument("device", help="device base URL, e.g. http://192.168.1.50")
        if name == "pull":
            p.add_argument("dir", help="directory to download into")
        p.add_argument("--bucket", default="fonts", help="bucket (default: fonts)")
        if name == "push":
            p.add_argument("--dir", help="local directory (default: media/<bucket>)")
            p.add_argument("--dry-run", action="store_true", help="only show what would be uploaded")
    args = ap.parse_args(argv)
    dev = Device(args.device, _credentials())
    {"ls": cmd_ls, "push": cmd_push, "pull": cmd_pull}[args.cmd](dev, args)


if __name__ == "__main__":
    main()
