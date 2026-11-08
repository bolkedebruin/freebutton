#!/usr/bin/env python3
"""
Generate MicroQuickJS stdlib header for FreeButton

This script compiles the custom stdlib definition (freebutton_stdlib.c) with
the mquickjs build tool to generate C header files containing the JavaScript
standard library structures. These headers are then included in the firmware.

Build Process:
1. Compile mquickjs_build.c and freebutton_stdlib.c as host executables
2. Run the stdlib builder with -m32 flag (ESP32 is 32-bit)
3. Generate freebutton_stdlib.h (stdlib structures)
4. Generate mquickjs_atom.h (atom table)

This follows the pattern documented in lib/mquickjs/README-ESP32.md
"""

import subprocess
import sys
import os
from pathlib import Path

def run_command(cmd, cwd=None):
    """Run a command and return success status"""
    print(f"Running: {' '.join(cmd)}")
    try:
        result = subprocess.run(cmd, cwd=cwd, check=True, capture_output=True, text=True)
        if result.stdout:
            print(result.stdout)
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error: Command failed with code {e.returncode}")
        if e.stdout:
            print("stdout:", e.stdout)
        if e.stderr:
            print("stderr:", e.stderr)
        return False

def main():
    # Get paths
    project_dir = Path(__file__).parent.parent.parent
    mquickjs_dir = project_dir / "lib" / "mquickjs"          # engine submodule (mquickjs_build.c, mqjs_stdlib.c, headers)
    src_scripting_dir = project_dir / "src" / "scripting"    # firmware bindings
    js_dir = src_scripting_dir / "js"                        # relocated FreeButton stdlib source
    generated_dir = src_scripting_dir / "generated"          # output for generated headers

    generated_dir.mkdir(parents=True, exist_ok=True)

    # Host build artifacts live in the generated dir (kept out of the submodule)
    build_obj = generated_dir / "mquickjs_build.host.o"
    stdlib_obj = generated_dir / "freebutton_stdlib.host.o"
    stdlib_tool = generated_dir / "freebutton_stdlib"
    stdlib_header = generated_dir / "freebutton_stdlib.h"
    atom_header = generated_dir / "mquickjs_atom.h"

    print("=" * 60)
    print("FreeButton MicroQuickJS stdlib generator")
    print("=" * 60)
    print(f"Project dir: {project_dir}")
    print(f"MQuickJS dir: {mquickjs_dir}")
    print(f"JS source dir: {js_dir}")
    print(f"Generated dir: {generated_dir}")
    print()

    # Compile mquickjs_build.c (from the engine submodule) as a host object
    print("Step 1: Compiling mquickjs_build.c...")
    if not run_command([
        "gcc", "-O2", "-Wall", "-D_GNU_SOURCE",
        f"-I{mquickjs_dir}",
        "-c", str(mquickjs_dir / "mquickjs_build.c"),
        "-o", str(build_obj)
    ]):
        return 1

    # Compile freebutton_stdlib.c (from src/scripting/js) as a host object.
    # It pulls in mquickjs_build.h and mqjs_stdlib.c from the engine submodule.
    print("\nStep 2: Compiling freebutton_stdlib.c...")
    if not run_command([
        "gcc", "-O2", "-Wall", "-D_GNU_SOURCE",
        f"-I{mquickjs_dir}", f"-I{src_scripting_dir}",
        "-c", str(js_dir / "freebutton_stdlib.c"),
        "-o", str(stdlib_obj)
    ]):
        return 1

    # Link into stdlib builder executable
    print("\nStep 3: Linking stdlib builder...")
    if not run_command([
        "gcc",
        str(stdlib_obj),
        str(build_obj),
        "-o", str(stdlib_tool)
    ]):
        return 1

    # Generate the headers (32-bit for ESP32). Each is written only when its
    # content changed: rewriting an identical header would make every build
    # recompile the engine, and opening it for writing before the tool ran
    # left an empty header behind when the tool failed.
    for step, header, args in (
        (4, stdlib_header, ["-m32"]),
        (5, atom_header, ["-a", "-m32"]),
    ):
        print(f"\nStep {step}: Generating {header.name}...")
        try:
            result = subprocess.run(
                [str(stdlib_tool), *args],
                check=True,
                capture_output=True,
                text=True
            )
        except subprocess.CalledProcessError as e:
            print(f"Error generating {header.name}: {e}")
            if e.stderr:
                print("stderr:", e.stderr)
            return 1
        if header.exists() and header.read_text() == result.stdout:
            print(f"Unchanged {header}")
        else:
            header.write_text(result.stdout)
            print(f"Generated {header}")

    print("\n" + "=" * 60)
    print("SUCCESS: stdlib headers generated!")
    print("=" * 60)
    print()
    print("Generated files:")
    print(f"  - {stdlib_header}")
    print(f"  - {atom_header}")
    print()
    print("These headers will be included in the firmware build.")

    return 0

if __name__ == "__main__":
    sys.exit(main())
