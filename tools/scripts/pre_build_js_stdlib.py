#!/usr/bin/env python3
"""
PlatformIO pre-build script for FreeButton JavaScript stdlib generation.

This script is called by PlatformIO before building the firmware.
It runs the stdlib generator to create the FreeButton stdlib headers.
"""

Import("env")

import subprocess
import sys
from pathlib import Path

def refuse_engine_host_headers(project_dir):
    """lib/mquickjs/mquickjs.c includes "mquickjs_atom.h" from its own
    directory first. A host build of the engine (`make` in lib/mquickjs, e.g.
    to run upstream's tests) leaves its own mquickjs_atom.h there, for the
    demo stdlib: the firmware's engine would then use the demo's atom indexes
    against the firmware's ROM table (seen on btn_a00001: JS_ATOM_empty 97
    instead of 134, string concatenation wrote into flash, boot loop)."""
    engine = project_dir / "lib" / "mquickjs"
    stray = [n for n in ("mquickjs_atom.h", "mqjs_stdlib.h", "example_stdlib.h")
             if (engine / n).exists()]
    if stray:
        print("\nERROR: host build outputs in lib/mquickjs would shadow the firmware's")
        print("generated stdlib headers: " + ", ".join(stray))
        print("Remove them with: make -C lib/mquickjs clean")
        sys.exit(1)


def generate_js_stdlib():
    """Generate JavaScript stdlib headers before building firmware"""
    project_dir = Path(env.get("PROJECT_DIR"))
    refuse_engine_host_headers(project_dir)
    script_path = project_dir / "tools" / "scripts" / "generate_js_stdlib.py"

    print("\n" + "="*60)
    print("Pre-build: Generating JavaScript stdlib headers")
    print("="*60)

    result = subprocess.run([sys.executable, str(script_path)], cwd=project_dir)

    if result.returncode != 0:
        print("\nERROR: Failed to generate JavaScript stdlib headers!")
        print("Build cannot continue without stdlib headers.")
        sys.exit(1)

    print("\n")

# Run now, while PlatformIO loads the pre: scripts, before SCons compiles
# anything. env.AddPreAction("buildprog", ...) ran only after every object
# file was built, so a fresh checkout failed on the missing headers
# (src/scripting/mquickjs_stdlib.c includes freebutton_stdlib.h). The
# generator rewrites a header only when it changed.
generate_js_stdlib()
