#!/usr/bin/env python3
"""Over-the-air update of a device's LittleFS image, keeping the user's files.

The filesystem holds both the web IDE bundle (www/), the fonts, and the
user's files (config.json, scripts/, the bytecode registry index
js_bytecode_meta.json, ...). A plain `uploadfs` or filesystem upload writes a
whole new image and so wipes the user's files. This tool:

  1. downloads every object outside www/ and fonts/ from the device's S3 API
     (GET /fs?list-type=2, GET /fs/<key>) into
     devices/<deviceName>/backup/fs-<timestamp>/ (git-ignored),
  2. assembles the image staging dir like the build does
     (tools/scripts/assemble_littlefs.py: data/ + web/dist/ gzipped), then
     puts the device's files on top (they win, except config.json when
     --config is given),
  3. builds the LittleFS image with PlatformIO's mklittlefs (same block /
     page / partition size as `pio run -t buildfs`),
  4. uploads it to POST /upload (updateType=filesystem) and applies it with
     POST /ota_update (reboot), then checks that the files are back.

The js_user partition (the bytecode itself) is not touched, so the registry
index restored from the device stays consistent with it.

usage:
  python3 tools/scripts/ota_fs.py http://192.168.1.50
  python3 tools/scripts/ota_fs.py http://192.168.1.50 --config devices/btn_a00001/config.json
  python3 tools/scripts/ota_fs.py http://192.168.1.50 --dry-run      # build only
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
PARTITIONS = os.path.join(PROJECT, "tools", "partition_tables", "button_v1.csv")
MKLITTLEFS = os.path.expanduser("~/.platformio/packages/tool-mklittlefs/mklittlefs")
S3NS = "{http://s3.amazonaws.com/doc/2006-03-01/}"
SYSTEM_PREFIXES = ("www/", "fonts/")


def http(method, url, data=None, headers=None, timeout=60):
    req = urllib.request.Request(url, data=data, method=method, headers=headers or {})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


def list_user_keys(base):
    keys, token = [], None
    while True:
        q = {"list-type": "2"}
        if token:
            q["continuation-token"] = token
        _, body = http("GET", f"{base}/fs?{urllib.parse.urlencode(q)}")
        root = ET.fromstring(body)
        for c in root.iter(f"{S3NS}Contents"):
            key = c.find(f"{S3NS}Key").text
            if not key.startswith(SYSTEM_PREFIXES):
                keys.append(key)
        if root.findtext(f"{S3NS}IsTruncated") != "true":
            return keys
        token = root.findtext(f"{S3NS}NextContinuationToken")


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
    args = ap.parse_args()
    base = args.device.rstrip("/")

    # 1. Device and its user files.
    _, info = http("GET", f"{base}/api/device")
    name = json.loads(info).get("deviceName") or "device"
    try:
        keys = list_user_keys(base)
    except urllib.error.HTTPError as e:
        sys.exit(f"the device has no S3 storage API ({e.code}); update its firmware first")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = os.path.join(PROJECT, "devices", name, "backup", f"fs-{stamp}")
    for key in keys:
        _, body = http("GET", f"{base}/fs/" + "/".join(urllib.parse.quote(s) for s in key.split("/")))
        dst = os.path.join(backup, key)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(body)
    print(f"[ota_fs] {name}: backed up {len(keys)} user files to {os.path.relpath(backup, PROJECT)}")

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
        shutil.copy2(os.path.join(backup, key), dst)

    # 3. Image (same parameters as `pio run -t buildfs`).
    size = data_partition_size()
    image = os.path.join(PROJECT, ".pio", f"ota_fs_{name}.bin")
    subprocess.run([MKLITTLEFS, "-c", STAGING, "-b", "4096", "-p", "256", "-s", str(size), image], check=True,
                   stdout=subprocess.DEVNULL)
    payload = open(image, "rb").read()
    if payload[8:16] != b"littlefs" or len(payload) != size:
        sys.exit("built image is not a LittleFS image of the partition size")
    print(f"[ota_fs] image {os.path.relpath(image, PROJECT)}: {len(payload)} bytes")
    if args.dry_run:
        return

    # 4. Upload and apply.
    body, ctype = multipart([("updateType", "filesystem")], "updateFile", "littlefs.bin", payload)
    try:
        status, text = http("POST", f"{base}/upload", data=body, headers={"Content-Type": ctype}, timeout=300)
    except urllib.error.HTTPError as e:
        sys.exit(f"upload refused ({e.code}): {e.read().decode(errors='replace')}")
    print(f"[ota_fs] upload: {status} {text.decode(errors='replace')}")
    try:
        http("POST", f"{base}/ota_update", timeout=10)
    except Exception:
        pass  # the device reboots before or while answering
    time.sleep(5)
    if not wait_for(base):
        sys.exit("device did not come back; the backup is in " + backup)
    time.sleep(3)
    after = set(list_user_keys(base))
    missing = [k for k in keys if k not in after and not (k == "config.json" and args.config)]
    if missing:
        sys.exit(f"files missing after the update: {missing} (backup in {backup})")
    print(f"[ota_fs] done: {len(after)} user files on the device, IDE updated")


if __name__ == "__main__":
    main()
