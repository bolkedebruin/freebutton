#!/usr/bin/env python3
"""Fingerprint of what makes mquickjs bytecode compatible with a firmware.

Bytecode refers to the stdlib's ROM atoms by offset, so a script compiled by
the web IDE's wasm compiler only runs correctly on firmware built from the
same stdlib ROM table, atom table and engine. This prints:

    <sha256>  freebutton_stdlib.h
    <sha256>  mquickjs_atom.h
    <commit>  lib/mquickjs

usage:
  wasm_stdlib_fingerprint.py <dir with freebutton_stdlib.h + mquickjs_atom.h>
      print the fingerprint (build_wasm.sh stores it next to the wasm)
  wasm_stdlib_fingerprint.py --check
      regenerate the firmware headers (tools/scripts/generate_js_stdlib.py)
      and compare with web/public/assets/mquickjs.stdlib.txt; exit 1 on drift
"""
import hashlib
import os
import subprocess
import sys

PROJECT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HEADERS = ("freebutton_stdlib.h", "mquickjs_atom.h")
STAMP = os.path.join(PROJECT, "web", "public", "assets", "mquickjs.stdlib.txt")


def fingerprint(header_dir):
    lines = []
    for name in HEADERS:
        with open(os.path.join(header_dir, name), "rb") as f:
            lines.append(f"{hashlib.sha256(f.read()).hexdigest()}  {name}")
    commit = subprocess.run(
        ["git", "-C", os.path.join(PROJECT, "lib", "mquickjs"), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True).stdout.strip()
    lines.append(f"{commit}  lib/mquickjs")
    return "\n".join(lines) + "\n"


def check():
    subprocess.run([sys.executable, os.path.join(PROJECT, "tools", "scripts", "generate_js_stdlib.py")],
                   check=True, stdout=subprocess.DEVNULL)
    current = fingerprint(os.path.join(PROJECT, "src", "scripting", "generated"))
    try:
        with open(STAMP) as f:
            stamped = f.read()
    except FileNotFoundError:
        stamped = ""
    if current == stamped:
        print("web IDE wasm compiler matches the firmware stdlib")
        return 0
    print("ERROR: web/public/assets/mquickjs.wasm was built from a different stdlib or engine\n"
          "than this firmware. Scripts compiled in the web IDE would reference the wrong\n"
          "ROM atoms. Rebuild it with: bash tools/js/build_wasm.sh (needs emcc) and commit\n"
          "web/public/assets/.\n", file=sys.stderr)
    print(f"--- committed {os.path.relpath(STAMP, PROJECT)}\n{stamped}--- firmware\n{current}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--check":
        sys.exit(check())
    if len(sys.argv) == 2:
        sys.stdout.write(fingerprint(sys.argv[1]))
        sys.exit(0)
    print(__doc__, file=sys.stderr)
    sys.exit(2)
