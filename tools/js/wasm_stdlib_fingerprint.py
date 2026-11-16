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
      and compare with web/public/assets/mquickjs.stdlib.txt; exit 1 on drift.
      Also checks the emulator wasm (below).

The emulator wasm (web/public/assets/fb-emulator.wasm, tools/js/
build_emulator_wasm.sh) runs scripts in the web IDE with the firmware's own
script glue compiled in, so it is stale as soon as one of those sources
changes. Its stamp is the fingerprint above plus one line per source:

    <sha256>  src/scripting/js/freebutton_mqtt.c
    ...

  wasm_stdlib_fingerprint.py --emulator-stamp <dir with the two headers>
      print the emulator stamp (build_emulator_wasm.sh stores it as
      web/public/assets/fb-emulator.stamp.txt)
"""
import glob
import hashlib
import os
import re
import subprocess
import sys

PROJECT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HEADERS = ("freebutton_stdlib.h", "mquickjs_atom.h")
STAMP = os.path.join(PROJECT, "web", "public", "assets", "mquickjs.stdlib.txt")
EMULATOR_STAMP = os.path.join(PROJECT, "web", "public", "assets", "fb-emulator.stamp.txt")

# What build_emulator_wasm.sh compiled: the files of
# tools/js/emulator/sources.txt (written by the build from the compilers'
# dependency lists: every firmware source and header that went in), plus the
# build script and everything in tools/js/emulator/. Besides: the engine (its
# commit is in the fingerprint) and the generated headers.
EMULATOR_SOURCE_LIST = os.path.join(PROJECT, "tools", "js", "emulator", "sources.txt")
EMULATOR_SOURCES = (
    "tools/js/build_emulator_wasm.sh",
    "tools/js/emulator/**/*",
)
# Single values the build script reads out of larger files: (file, regex, name).
EMULATOR_CONSTANTS = (
    ("src/drivers/ButtonManager.h", r"DEFAULT_UPDATE_INTERVAL *= *([0-9]+);", "ButtonManager::DEFAULT_UPDATE_INTERVAL"),
    ("sdkconfig.esp32", r"^CONFIG_MQTT_BUFFER_SIZE=([0-9]+)$", "CONFIG_MQTT_BUFFER_SIZE"),
    ("src/drivers/NeoPixel.h", r"NUMBER_OF_LEDS *= *([0-9]+);", "NeoPixelController::NUMBER_OF_LEDS"),
    ("src/main.cpp", r"^    delay\(([0-9]+)\);$", "loop() delay"),
    ("sdkconfig.esp32", r"^CONFIG_MQTT_EVENT_QUEUE_SIZE=([0-9]+)$", "CONFIG_MQTT_EVENT_QUEUE_SIZE"),
    ("src/main.cpp", r"^        vTaskDelay\(pdMS_TO_TICKS\(([0-9]+)\)\);$", "lvgl_task() delay"),
    # The JSON libraries the firmware sources are compiled with (.pio/libdeps).
    ("platformio.ini", r"bblanchon/ArduinoJson @ ([0-9.]+)", "ArduinoJson"),
    ("platformio.ini", r"bolkedebruin/rapidjson\.git#([0-9a-f]+)", "RapidJSON"),
    # LVGL is compiled into the emulator from .pio/libdeps/esp32/lvgl; the
    # build refuses any other version than the one pinned here.
    ("platformio.ini", r"lvgl/lvgl @ ([0-9.]+)", "LVGL"),
)


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


def emulator_stamp(header_dir):
    lines = [fingerprint(header_dir)]
    files = set()
    try:
        with open(EMULATOR_SOURCE_LIST) as f:
            files.update(line.strip() for line in f if line.strip())
    except FileNotFoundError:
        pass
    for pattern in EMULATOR_SOURCES:
        for path in glob.glob(os.path.join(PROJECT, pattern), recursive=True):
            if os.path.isfile(path) and "__pycache__" not in path:
                files.add(os.path.relpath(path, PROJECT).replace(os.sep, "/"))
    for rel in sorted(files):
        try:
            with open(os.path.join(PROJECT, rel), "rb") as f:
                data = f.read()
        except FileNotFoundError:
            lines.append(f"{'missing':64}  {rel}\n")
            continue
        if rel == "include/config.h":
            # The version string changes with every release
            # (tools/scripts/sync_version.py); system.version() in the
            # emulator is the one of its build.
            data = b"\n".join(line for line in data.split(b"\n") if not line.startswith(b"#define FIRMWARE "))
        lines.append(f"{hashlib.sha256(data).hexdigest()}  {rel}\n")
    for rel, pattern, name in EMULATOR_CONSTANTS:
        with open(os.path.join(PROJECT, rel)) as f:
            match = re.search(pattern, f.read(), re.M)
        lines.append(f"{match.group(1) if match else '?'}  {name}\n")
    return "".join(lines)


def check_emulator():
    try:
        with open(EMULATOR_STAMP) as f:
            stamped = f.read()
    except FileNotFoundError:
        print("no emulator wasm stamp (web/public/assets/fb-emulator.stamp.txt): not checked")
        return 0
    current = emulator_stamp(os.path.join(PROJECT, "src", "scripting", "generated"))
    if current == stamped:
        print("web IDE emulator wasm matches the firmware sources")
        return 0
    print("ERROR: web/public/assets/fb-emulator.wasm was built from other sources than this\n"
          "checkout. The web IDE's emulator would run scripts with script bindings the firmware\n"
          "no longer has. Rebuild it with: bash tools/js/build_emulator_wasm.sh (needs emcc)\n"
          "and commit web/public/assets/fb-emulator.wasm and fb-emulator.stamp.txt.\n", file=sys.stderr)
    was = dict(reversed(line.split("  ", 1)) for line in stamped.splitlines() if "  " in line)
    now = dict(reversed(line.split("  ", 1)) for line in current.splitlines() if "  " in line)
    for name in sorted(set(was) | set(now)):
        if was.get(name) != now.get(name):
            state = "added" if name not in was else "removed" if name not in now else "changed"
            print(f"  {state}: {name}", file=sys.stderr)
    return 1


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
        return check_emulator()
    print("ERROR: web/public/assets/mquickjs.wasm was built from a different stdlib or engine\n"
          "than this firmware. Scripts compiled in the web IDE would reference the wrong\n"
          "ROM atoms. Rebuild it with: bash tools/js/build_wasm.sh (needs emcc) and commit\n"
          "web/public/assets/.\n", file=sys.stderr)
    print(f"--- committed {os.path.relpath(STAMP, PROJECT)}\n{stamped}--- firmware\n{current}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--check":
        sys.exit(check())
    if len(sys.argv) == 3 and sys.argv[1] == "--emulator-stamp":
        sys.stdout.write(emulator_stamp(sys.argv[2]))
        sys.exit(0)
    if len(sys.argv) == 2:
        sys.stdout.write(fingerprint(sys.argv[1]))
        sys.exit(0)
    print(__doc__, file=sys.stderr)
    sys.exit(2)
