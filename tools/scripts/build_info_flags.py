#!/usr/bin/env python3
"""
PlatformIO pre-script: inject build metadata as compile-time CPPDEFINES.

Runs before the build (referenced from platformio.ini extra_scripts). It exposes
the following string/int macros to the firmware:

  FIRMWARE_VERSION  - version from repo-root VERSION file (e.g. "3.1.0-alpha.1")
  GIT_COMMIT        - short commit hash (or "unknown")
  GIT_BRANCH        - current branch (or "unknown")
  BUILD_DIRTY       - 1 if the working tree is dirty, else 0
  BUILD_TIMESTAMP   - UTC build time, "YYYYMMDDHHMMSS"

Never fails the build: if git or the VERSION file is unavailable, it falls back
to "unknown"/0.
"""

import sys
from datetime import datetime, timezone
from pathlib import Path

Import("env")  # noqa: F821 - provided by PlatformIO SCons at runtime

# SCons exec context has no __file__; derive paths from the project directory.
REPO_ROOT = Path(env.subst("$PROJECT_DIR")).resolve()  # noqa: F821
SCRIPTS_DIR = REPO_ROOT / "tools" / "scripts"
VERSION_FILE = REPO_ROOT / "VERSION"

# Reuse the git logic from build_info.py (same directory).
sys.path.insert(0, str(SCRIPTS_DIR))
try:
    from build_info import get_git_info
except Exception:  # defensive fallback
    def get_git_info():
        return {"commit": "unknown", "branch": "unknown", "dirty": False}


def read_version():
    try:
        version = VERSION_FILE.read_text().strip()
        return version or "unknown"
    except Exception:
        return "unknown"


git_info = get_git_info()
version = read_version()
timestamp = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")

commit = git_info.get("commit") or "unknown"
branch = git_info.get("branch") or "unknown"
dirty = 1 if git_info.get("dirty") else 0

env.Append(CPPDEFINES=[  # noqa: F821
    ("FIRMWARE_VERSION", env.StringifyMacro(version)),   # noqa: F821
    ("GIT_COMMIT", env.StringifyMacro(commit)),          # noqa: F821
    ("GIT_BRANCH", env.StringifyMacro(branch)),          # noqa: F821
    ("BUILD_TIMESTAMP", env.StringifyMacro(timestamp)),  # noqa: F821
    ("BUILD_DIRTY", dirty),
])

print(f"[build_info] version={version} commit={commit} branch={branch} "
      f"dirty={dirty} timestamp={timestamp}")
