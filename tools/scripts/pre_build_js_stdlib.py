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

def generate_js_stdlib(target, source, env):
    """Generate JavaScript stdlib headers before building firmware"""
    project_dir = Path(env.get("PROJECT_DIR"))
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

# Register the pre-build action
env.AddPreAction("buildprog", generate_js_stdlib)
