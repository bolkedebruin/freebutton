#!/usr/bin/env python3
"""Decode the heap probe's serial output (src/debug/heap_probe.cpp).

Usage: heap_probe_decode.py <serial log> [block]

block: index of the HEAPPROBE block to decode; default "low", the block with
the lowest free heap. Prints the bytes alive at that moment, summed by call
site (allocator wrappers are skipped; DEPTH=<n> sets how many callers are
shown, default 4). Needs the firmware.elf of the build that printed the log.
"""
import os
import subprocess
import sys

ELF = os.environ.get("ELF", ".pio/build/esp32/firmware.elf")
A2L = os.environ.get("ADDR2LINE", os.path.expanduser(
    "~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line"))
SKIP = ("heap_caps", "trace_malloc", "__wrap", "_malloc_r", "malloc@", "calloc@", "_calloc_r",
        "operator new", "pvPortMalloc", "realloc", "_realloc_r")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    blocks = open(sys.argv[1], errors="replace").read().split("HEAPPROBE ")[1:]
    if not blocks:
        sys.exit("no HEAPPROBE block in the log")
    which = sys.argv[2] if len(sys.argv) > 2 else "low"
    if which == "low":
        index = min(range(len(blocks)), key=lambda i: int(blocks[i].split()[0].split("=")[1]))
    else:
        index = int(which) % len(blocks)
    block = blocks[index]
    print("HEAPPROBE", block.split("\n")[0], "(block %d of %d)" % (index, len(blocks)))

    sites = [line.split()[1:] for line in block.split("HEAPEND")[0].split("\n") if line.startswith("HEAPSITE")]
    addrs = sorted({a for s in sites for a in s[2:] if a not in ("0x0", "(nil)")})
    out = subprocess.run([A2L, "-e", ELF, "-f", "-C", "-s"] + addrs, capture_output=True, text=True).stdout.split("\n")
    name = {a: out[2 * i] + "@" + out[2 * i + 1].split(":")[-1].split(" ")[0] for i, a in enumerate(addrs)}

    depth = int(os.environ.get("DEPTH", "4"))
    total = {}
    for s in sites:
        raw = [name[a] for a in s[2:] if a in name]
        frames = [f for f in raw if not f.startswith(SKIP)]
        if not frames:
            frames = ["(" + " < ".join(f.split("(")[0][-60:] for f in raw[-2:]) + ")"]
        entry = total.setdefault(" < ".join(frames[:depth]), [0, 0])
        entry[0] += int(s[0])
        entry[1] += int(s[1])
    for key, (size, count) in sorted(total.items(), key=lambda kv: -kv[1][0]):
        print("%6d B %3d x  %s" % (size, count, key))


if __name__ == "__main__":
    main()
