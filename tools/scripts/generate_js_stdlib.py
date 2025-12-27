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
    mquickjs_dir = project_dir / "lib" / "mquickjs"
    src_scripting_dir = project_dir / "src" / "scripting"

    print("=" * 60)
    print("FreeButton MicroQuickJS stdlib generator")
    print("=" * 60)
    print(f"Project dir: {project_dir}")
    print(f"MQuickJS dir: {mquickjs_dir}")
    print()

    # Change to mquickjs directory
    os.chdir(mquickjs_dir)

    # Compile mquickjs_build.c as host executable
    print("Step 1: Compiling mquickjs_build.c...")
    if not run_command([
        "gcc", "-O2", "-Wall", "-D_GNU_SOURCE",
        "-c", "mquickjs_build.c",
        "-o", "mquickjs_build.host.o"
    ]):
        return 1

    # Compile freebutton_stdlib.c as host executable
    print("\nStep 2: Compiling freebutton_stdlib.c...")
    if not run_command([
        "gcc", "-O2", "-Wall", "-D_GNU_SOURCE",
        "-I.", f"-I{src_scripting_dir}",
        "-c", "freebutton_stdlib.c",
        "-o", "freebutton_stdlib.host.o"
    ]):
        return 1

    # Link into stdlib builder executable
    print("\nStep 3: Linking stdlib builder...")
    if not run_command([
        "gcc",
        "freebutton_stdlib.host.o",
        "mquickjs_build.host.o",
        "-o", "freebutton_stdlib"
    ]):
        return 1

    # Generate stdlib header (32-bit for ESP32)
    print("\nStep 4: Generating freebutton_stdlib.h...")
    try:
        with open("freebutton_stdlib.h", "w") as f:
            result = subprocess.run(
                ["./freebutton_stdlib", "-m32"],
                check=True,
                capture_output=True,
                text=True
            )
            f.write(result.stdout)
        print("Generated freebutton_stdlib.h")
    except subprocess.CalledProcessError as e:
        print(f"Error generating stdlib header: {e}")
        if e.stderr:
            print("stderr:", e.stderr)
        return 1

    # Generate atom header (32-bit for ESP32)
    print("\nStep 5: Generating mquickjs_atom.h...")
    try:
        with open("mquickjs_atom.h", "w") as f:
            result = subprocess.run(
                ["./freebutton_stdlib", "-a", "-m32"],
                check=True,
                capture_output=True,
                text=True
            )
            f.write(result.stdout)
        print("Generated mquickjs_atom.h")
    except subprocess.CalledProcessError as e:
        print(f"Error generating atom header: {e}")
        if e.stderr:
            print("stderr:", e.stderr)
        return 1

    print("\n" + "=" * 60)
    print("SUCCESS: stdlib headers generated!")
    print("=" * 60)
    print()
    print("Generated files:")
    print(f"  - {mquickjs_dir}/freebutton_stdlib.h")
    print(f"  - {mquickjs_dir}/mquickjs_atom.h")
    print()
    print("These headers will be included in the firmware build.")

    return 0

if __name__ == "__main__":
    sys.exit(main())
