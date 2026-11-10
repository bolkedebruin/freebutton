#!/usr/bin/env python3
"""Build the media partition image from media/<bucket>/<key> and flash it.

Builds tools/media/mkmedia, packs every file under the media directory
(media/fonts/medium.ttf -> bucket "fonts", key "medium.ttf"; deeper paths
keep their '/'), verifies the image (mkmedia --list) and writes it to the
`media` partition with PlatformIO's esptool. The partition offset and size
come from the partition CSV; without one (or without a `media` row) the
values of docs/MEDIA-STORE.md are used.

  tools/scripts/flash_media.py [--port /dev/cu.usbmodem1101]
  tools/scripts/flash_media.py --no-flash            # only build + verify
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUCKETS = ("fonts", "images", "render")
DEFAULT_OFFSET = 0x660000
DEFAULT_SIZE = 0x500000
PIO = os.path.expanduser("~/.platformio")
ESPTOOL = [os.path.join(PIO, "penv", "bin", "python"), os.path.join(PIO, "packages", "tool-esptoolpy", "esptool.py")]


def parse_size(text):
    t = text.strip().upper()
    mult = 1
    if t.endswith("K"):
        mult, t = 1024, t[:-1]
    elif t.endswith("M"):
        mult, t = 1024 * 1024, t[:-1]
    return int(t, 0) * mult


def media_partition(csv_path):
    """(offset, size) of the `media` partition, offsets filled in like
    gen_esp32part.py (start 0x9000, app 64 KB aligned, data 4 KB)."""
    offset = 0x9000
    with open(csv_path) as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) < 5:
                continue
            name, ptype, _sub, off, size = cols[:5]
            align = 0x10000 if ptype == "app" else 0x1000
            start = parse_size(off) if off else (offset + align - 1) // align * align
            length = parse_size(size)
            if name == "media":
                return start, length
            offset = start + length
    return None


def collect(media_dir):
    specs = []
    if not os.path.isdir(media_dir):
        return specs
    for bucket in sorted(os.listdir(media_dir)):
        bdir = os.path.join(media_dir, bucket)
        if bucket.startswith(".") or not os.path.isdir(bdir):
            continue
        if bucket not in BUCKETS:
            print(f"flash_media: skipping {bdir}: not a bucket ({', '.join(BUCKETS)})", file=sys.stderr)
            continue
        for dirpath, dirnames, filenames in os.walk(bdir):
            dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
            for name in sorted(filenames):
                if name.startswith("."):
                    continue
                path = os.path.join(dirpath, name)
                key = os.path.relpath(path, bdir).replace(os.sep, "/")
                specs.append(f"{bucket}/{key}={path}")
    return specs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--partitions", default=os.path.join(ROOT, "tools", "partition_tables", "button_v2.csv"))
    ap.add_argument("--media-dir", default=os.path.join(ROOT, "media"))
    ap.add_argument("--out", default=os.path.join(ROOT, ".pio", "media", "media.bin"))
    ap.add_argument("--mtime", type=int, help="mtime for every file (default: the files' own)")
    ap.add_argument("--port", help="serial port for esptool (default: esptool's auto-detect)")
    ap.add_argument("--no-flash", action="store_true", help="build and verify the image only")
    ap.add_argument("--dry-run", action="store_true", help="print the flash command instead of running it")
    args = ap.parse_args()

    part = None
    if os.path.exists(args.partitions):
        part = media_partition(args.partitions)
        if part is None:
            print(f"flash_media: no 'media' partition in {args.partitions}", file=sys.stderr)
    else:
        print(f"flash_media: {args.partitions} not found", file=sys.stderr)
    if part is None:
        part = (DEFAULT_OFFSET, DEFAULT_SIZE)
        print(f"flash_media: using offset 0x{part[0]:x}, size 0x{part[1]:x} (docs/MEDIA-STORE.md)", file=sys.stderr)
    offset, size = part
    if offset % 0x10000 or size % 0x10000:
        sys.exit(f"flash_media: media partition 0x{offset:x}+0x{size:x} is not 64 KB aligned")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    mkmedia = os.path.join(os.path.dirname(os.path.abspath(args.out)), "mkmedia")
    subprocess.run(["sh", os.path.join(ROOT, "tools", "media", "build.sh"), mkmedia], check=True,
                   stdout=subprocess.DEVNULL)

    specs = collect(args.media_dir)
    if not specs:
        print(f"flash_media: no files under {args.media_dir}: the image is an empty store", file=sys.stderr)
    cmd = [mkmedia, args.out, "--size", hex(size)]
    if args.mtime is not None:
        cmd += ["--mtime", str(args.mtime)]
    subprocess.run(cmd + specs, check=True)
    subprocess.run([mkmedia, "--list", args.out], check=True)

    flash = ESPTOOL + ["--chip", "esp32s3"]
    if args.port:
        flash += ["--port", args.port]
    flash += ["write_flash", hex(offset), args.out]
    if args.no_flash:
        print(f"image: {args.out} (media partition at 0x{offset:x}, 0x{size:x} bytes)")
        return
    print(" ".join(flash))
    if args.dry_run:
        return
    subprocess.run(flash, check=True)


if __name__ == "__main__":
    main()
