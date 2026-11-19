#!/usr/bin/env python3
"""Over-the-air update of a device's LittleFS image, keeping the user's files.

The LittleFS `data` partition holds the web IDE bundle (www/), the examples
and the user's files (config.json, scripts/, ...). A plain `uploadfs` or
filesystem upload writes a whole new image and so wipes the user's files.
This tool:

  1. downloads every object of the device's S3 bucket `app` (the user's
     files: the LittleFS root without the directories of the read-only
     buckets web and examples, which come from the build) with
     GET /app?list-type=2, GET /app/<key> into
     devices/<deviceName>/backup/fs-<timestamp>/app/ (git-ignored). An `app`
     key is the LittleFS path without the leading slash. It also backs up
     the bucket `fonts` into .../fs-<timestamp>/fonts/: that bucket lives in
     the raw `media` partition (docs/MEDIA-STORE.md), not in LittleFS, so it
     is never put into the image and this tool never changes it (restore it
     with tools/scripts/media_sync.py push --dir .../fonts);
  2. assembles the image staging dir like the build does
     (tools/scripts/assemble_littlefs.py: data/ + web/dist/ gzipped), then
     puts the device's files on top (they win, except config.json when
     --config is given),
  3. builds the LittleFS image with PlatformIO's mklittlefs (same block /
     page / partition size as `pio run -t buildfs`, size from
     tools/partition_tables/button_v2.csv),
  4. uploads it to POST /upload (updateType=filesystem) and applies it with
     POST /ota_update (reboot), then checks that the files are back.

The compiled scripts are not part of this: they and their index are in the
js_user partition (docs/SCRIPT-STORE.md), which a filesystem update does not
touch.

--restore DIR takes the user files from DIR (an earlier backup's app/
directory) instead of from the device, e.g. after the full erase of a
partition-table migration (docs/MIGRATION-PARTITIONS-V2.md). The device's
current user files are still backed up first.

usage:
  python3 tools/scripts/ota_fs.py http://192.168.1.50
  python3 tools/scripts/ota_fs.py http://192.168.1.50 --config devices/btn_a00001/config.json
  python3 tools/scripts/ota_fs.py http://192.168.1.50 --dry-run      # back up + build only
  python3 tools/scripts/ota_fs.py http://192.168.1.50 --restore devices/btn_a00001/backup/fs-20261110-120000/app
Build the web IDE first (cd web && npm run build).
"""
import argparse
import csv
import datetime
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import xml.etree.ElementTree as ET

PROJECT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
STAGING = os.path.join(PROJECT, ".pio", "staging_data")
PARTITIONS = os.path.join(PROJECT, "tools", "partition_tables", "button_v2.csv")
MKLITTLEFS = os.path.expanduser("~/.platformio/packages/tool-mklittlefs/mklittlefs")
S3NS = "{http://s3.amazonaws.com/doc/2006-03-01/}"
BUCKET = "app"  # the user's files; key = LittleFS path without the leading "/"
MEDIA_BUCKETS = ("fonts",)  # media partition: backed up, never put into the image
# The bytecode index of firmware before the script store. A backup made then
# still has it; it describes a js_user that is not restored with the files.
OLD_BYTECODE_INDEX = "js_bytecode_meta.json"


# Device access credentials (shown on the device display). Without them,
# requests go out unsigned (firmware without authentication still works).
CREDENTIALS = None  # (access key id, secret) once main() has parsed them


def _sign(method, url, headers, data):
    """AWS SigV4 (region us-east-1, service s3), header form. The path is
    signed exactly as sent; the query is decoded, re-encoded and sorted.
    Multipart bodies use UNSIGNED-PAYLOAD (the device cannot hash them)."""
    import hashlib
    import hmac
    akid, secret = CREDENTIALS
    u = urllib.parse.urlsplit(url)
    now = datetime.datetime.now(datetime.timezone.utc)
    amz_date, day = now.strftime("%Y%m%dT%H%M%SZ"), now.strftime("%Y%m%d")
    is_multipart = headers.get("Content-Type", "").startswith("multipart/")
    payload = "UNSIGNED-PAYLOAD" if is_multipart else hashlib.sha256(data or b"").hexdigest()
    host = u.netloc
    pairs = sorted((urllib.parse.quote(k, safe="-_.~"), urllib.parse.quote(v, safe="-_.~"))
                   for k, v in urllib.parse.parse_qsl(u.query, keep_blank_values=True))
    canonical_query = "&".join(f"{k}={v}" for k, v in pairs)
    signed = "host;x-amz-content-sha256;x-amz-date"
    canonical = "\n".join([method, u.path or "/", canonical_query,
                           f"host:{host}\nx-amz-content-sha256:{payload}\nx-amz-date:{amz_date}\n",
                           signed, payload])
    scope = f"{day}/us-east-1/s3/aws4_request"
    to_sign = "\n".join(["AWS4-HMAC-SHA256", amz_date, scope, hashlib.sha256(canonical.encode()).hexdigest()])
    key = ("AWS4" + secret).encode()
    for part in (day, "us-east-1", "s3", "aws4_request"):
        key = hmac.new(key, part.encode(), hashlib.sha256).digest()
    signature = hmac.new(key, to_sign.encode(), hashlib.sha256).hexdigest()
    headers.update({
        "x-amz-date": amz_date,
        "x-amz-content-sha256": payload,
        "Authorization": f"AWS4-HMAC-SHA256 Credential={akid}/{scope}, SignedHeaders={signed}, Signature={signature}",
    })


def http(method, url, data=None, headers=None, timeout=60):
    headers = dict(headers or {})
    if CREDENTIALS:
        _sign(method, url, headers, data)
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


def list_user_keys(base, bucket=BUCKET):
    keys, token = [], None
    while True:
        q = {"list-type": "2"}
        if token:
            q["continuation-token"] = token
        _, body = http("GET", f"{base}/{bucket}?{urllib.parse.urlencode(q)}")
        root = ET.fromstring(body)
        for c in root.iter(f"{S3NS}Contents"):
            keys.append(c.find(f"{S3NS}Key").text)
        if root.findtext(f"{S3NS}IsTruncated") != "true":
            return keys
        token = root.findtext(f"{S3NS}NextContinuationToken")
        if not token:
            return keys


def download_bucket(base, bucket, keys, dst_dir):
    """GET every key of bucket into dst_dir/<key>; returns the keys saved."""
    saved = []
    for key in keys:
        parts = key.split("/")
        if not key or any(p in ("", ".", "..") for p in parts):
            print(f"[ota_fs] skipping unsafe key {key!r} in bucket {bucket}", file=sys.stderr)
            continue
        _, body = http("GET", f"{base}/{bucket}/" + "/".join(urllib.parse.quote(p, safe="") for p in parts))
        dst = os.path.join(dst_dir, *parts)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(body)
        saved.append(key)
    return saved


def local_keys(directory):
    """Keys (relative paths, '/'-separated) of every file below directory."""
    keys = []
    for root, _dirs, names in os.walk(directory):
        for name in names:
            keys.append(os.path.relpath(os.path.join(root, name), directory).replace(os.sep, "/"))
    return sorted(keys)


def data_partition_size():
    with open(PARTITIONS) as f:
        for row in csv.reader(line for line in f if not line.lstrip().startswith("#")):
            if row and row[0].strip() == "data":
                return int(row[4].strip(), 0)
    sys.exit("data partition not found in " + PARTITIONS)


def multipart(fields, file_field, filename, payload):
    boundary = uuid.uuid4().hex
    parts = []
    for name, value in fields:  # plain fields first: the device reads updateType when the file starts
        parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n'.encode())
    parts.append((f'--{boundary}\r\nContent-Disposition: form-data; name="{file_field}"; filename="{filename}"\r\n'
                  'Content-Type: application/octet-stream\r\n\r\n').encode())
    parts.append(payload)
    parts.append(f"\r\n--{boundary}--\r\n".encode())
    return b"".join(parts), f"multipart/form-data; boundary={boundary}"


def wait_for(base, seconds=90):
    end = time.time() + seconds
    while time.time() < end:
        try:
            http("GET", f"{base}/api/version", timeout=3)
            return True
        except Exception:
            time.sleep(2)
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("device", help="device base URL, e.g. http://192.168.1.50")
    ap.add_argument("--config", help="use this config.json instead of the device's")
    ap.add_argument("--dry-run", action="store_true", help="back up and build the image, do not upload")
    ap.add_argument("--restore", metavar="DIR",
                    help="take the user files from DIR (a backup's app/ dir) instead of from the device")
    ap.add_argument("--access-key", default=os.environ.get("FB_ACCESS_KEY_ID"),
                    help="device access key id (default $FB_ACCESS_KEY_ID; shown on the device display)")
    ap.add_argument("--secret-key", default=os.environ.get("FB_SECRET_ACCESS_KEY"),
                    help="device secret (default $FB_SECRET_ACCESS_KEY)")
    args = ap.parse_args()
    global CREDENTIALS
    if args.access_key and args.secret_key:
        CREDENTIALS = (args.access_key, args.secret_key)
    base = args.device.rstrip("/")

    # 1. Device and its user files.
    _, info = http("GET", f"{base}/api/device")
    name = json.loads(info).get("deviceName") or "device"
    try:
        keys = list_user_keys(base)
    except urllib.error.HTTPError as e:
        if e.code == 403 and e.headers.get("x-freebutton-auth"):
            sys.exit("the device requires a signature: pass --access-key/--secret-key or set "
                     "FB_ACCESS_KEY_ID/FB_SECRET_ACCESS_KEY (hold the factory button 6 s to show them)")
        sys.exit(f"the device has no S3 bucket '{BUCKET}' ({e.code}); update its firmware first")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_root = os.path.join(PROJECT, "devices", name, "backup", f"fs-{stamp}")
    backup = os.path.join(backup_root, BUCKET)
    keys = download_bucket(base, BUCKET, keys, backup)
    print(f"[ota_fs] {name}: backed up {len(keys)} user files to {os.path.relpath(backup, PROJECT)}")
    # Media buckets (raw media partition): backed up only, never imaged.
    for bucket in MEDIA_BUCKETS:
        try:
            mkeys = list_user_keys(base, bucket)
        except urllib.error.HTTPError as e:
            print(f"[ota_fs] bucket '{bucket}' not backed up ({e.code})", file=sys.stderr)
            continue
        download_bucket(base, bucket, mkeys, os.path.join(backup_root, bucket))
        print(f"[ota_fs] {name}: backed up {len(mkeys)} objects of bucket {bucket} to "
              f"{os.path.relpath(os.path.join(backup_root, bucket), PROJECT)} (not part of the image)")

    # The files that go into the image: the device's, or --restore DIR.
    source = backup
    if args.restore:
        source = os.path.abspath(args.restore)
        if not os.path.isdir(source):
            sys.exit(f"--restore: no directory {source}")
        if os.path.isdir(os.path.join(source, BUCKET)) and not os.path.exists(os.path.join(source, "config.json")):
            sys.exit(f"--restore: {source} looks like a backup root; pass its {BUCKET}/ directory")
        keys = local_keys(source)
        if OLD_BYTECODE_INDEX in keys:
            keys.remove(OLD_BYTECODE_INDEX)
            print(f"[ota_fs] --restore: {OLD_BYTECODE_INDEX} (old firmware's bytecode index) is not put into the image")
        print(f"[ota_fs] --restore: {len(keys)} user files from {source}")

    # 2. Staging dir: build contents, then the device's files on top.
    env = dict(os.environ)
    if args.config:
        env["FREEBUTTON_CONFIG"] = args.config
    subprocess.run([sys.executable, os.path.join(PROJECT, "tools", "scripts", "assemble_littlefs.py")],
                   check=True, env=env, cwd=PROJECT)
    if not os.path.isfile(os.path.join(STAGING, "www", "index.html.gz")):
        sys.exit("no web IDE in the staging dir: run `cd web && npm run build` first")
    for key in keys:
        if key == "config.json" and args.config:
            continue
        dst = os.path.join(STAGING, key)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(os.path.join(source, *key.split("/")), dst)

    # 3. Image (same parameters as `pio run -t buildfs`).
    size = data_partition_size()
    image = os.path.join(PROJECT, ".pio", f"ota_fs_{name}.bin")
    subprocess.run([MKLITTLEFS, "-c", STAGING, "-b", "4096", "-p", "256", "-s", str(size), image], check=True,
                   stdout=subprocess.DEVNULL)
    payload = open(image, "rb").read()
    if payload[8:16] != b"littlefs" or len(payload) != size:
        sys.exit("built image is not a LittleFS image of the partition size")
    print(f"[ota_fs] image {os.path.relpath(image, PROJECT)}: {len(payload)} bytes (data partition 0x{size:X})")
    if args.dry_run:
        return

    # 4. Upload and apply.
    body, ctype = multipart([("updateType", "filesystem")], "updateFile", "littlefs.bin", payload)
    try:
        # The signature covers the URL, not the multipart body: the URL
        # carries the image's hash, which the device checks.
        import hashlib
        status, text = http("POST", f"{base}/upload?sha256={hashlib.sha256(payload).hexdigest()}", data=body,
                            headers={"Content-Type": ctype}, timeout=300)
    except urllib.error.HTTPError as e:
        sys.exit(f"upload refused ({e.code}): {e.read().decode(errors='replace')}")
    print(f"[ota_fs] upload: {status} {text.decode(errors='replace')}")
    try:
        http("POST", f"{base}/ota_update", timeout=10)
    except Exception:
        pass  # the device reboots before or while answering
    time.sleep(5)
    if not wait_for(base):
        sys.exit("device did not come back; the backup is in " + backup_root)
    time.sleep(3)
    after = set(list_user_keys(base))
    missing = [k for k in keys if k not in after and not (k == "config.json" and args.config)]
    if missing:
        sys.exit(f"files missing after the update: {missing} (backup in {backup})")
    print(f"[ota_fs] done: {len(after)} user files on the device, IDE updated")


if __name__ == "__main__":
    main()
