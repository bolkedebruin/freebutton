#!/usr/bin/env python3
"""
Version synchronization script for FreeButton.

Single source of truth: repo-root VERSION file (one line, e.g. 3.1.0-alpha.1).
This script propagates that version into the places that must stay in lockstep:

  1. include/config.h        -> #define FIRMWARE "FreeButton <VERSION>"
                                (preserves the "FreeButton " prefix; only the
                                 version token is substituted)
  2. web/package.json        -> "version": "<VERSION>"

Note: lib/mquickjs/library.json is intentionally NOT touched here -- it now
lives in the mquickjs submodule (engine-only) and is outside this repo's
version tracking.

Usage:
  python3 tools/scripts/sync_version.py            # sync from VERSION file
  python3 tools/scripts/sync_version.py 3.2.0      # override VERSION + sync
  python3 tools/scripts/sync_version.py v3.2.0     # leading 'v' is stripped
  python3 tools/scripts/sync_version.py --check     # verify, non-zero on drift

Replaces the buggy update_version.py, which clobbered the "FreeButton " prefix.
"""

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
VERSION_FILE = REPO_ROOT / "VERSION"
CONFIG_H = REPO_ROOT / "include" / "config.h"
PACKAGE_JSON = REPO_ROOT / "web" / "package.json"

# Match: #define FIRMWARE "FreeButton <anything>"
# Group 1 = prefix through the opening quote + "FreeButton "; group 2 = closing quote.
FIRMWARE_RE = re.compile(r'(#define\s+FIRMWARE\s+"FreeButton\s+)[^"]*(")')
# package.json: "version": "<anything>"
PACKAGE_VERSION_RE = re.compile(r'("version"\s*:\s*")[^"]*(")')


def read_version_file():
    if not VERSION_FILE.exists():
        print(f"Error: VERSION file not found at {VERSION_FILE}")
        sys.exit(1)
    version = VERSION_FILE.read_text().strip()
    if not version:
        print(f"Error: VERSION file at {VERSION_FILE} is empty")
        sys.exit(1)
    return version


def write_version_file(version):
    VERSION_FILE.write_text(version + "\n")


def normalize(version):
    version = version.strip()
    if version.startswith("v"):
        version = version[1:]
    return version


def config_current_version(content):
    m = FIRMWARE_RE.search(content)
    return m.group(0)[len('#define FIRMWARE "FreeButton '):-1] if m else None


def package_current_version(content):
    m = PACKAGE_VERSION_RE.search(content)
    return m.group(0)[len('"version": "'):-1] if m else None


def sync_config_h(version):
    if not CONFIG_H.exists():
        print(f"Error: config.h not found at {CONFIG_H}")
        sys.exit(1)
    content = CONFIG_H.read_text()
    if not FIRMWARE_RE.search(content):
        print(f'Error: no `#define FIRMWARE "FreeButton ..."` found in {CONFIG_H}')
        sys.exit(1)
    new_content = FIRMWARE_RE.sub(rf'\g<1>{version}\g<2>', content)
    if new_content != content:
        CONFIG_H.write_text(new_content)
        print(f'Updated {CONFIG_H.relative_to(REPO_ROOT)} -> FreeButton {version}')
    else:
        print(f'{CONFIG_H.relative_to(REPO_ROOT)} already at FreeButton {version}')


def sync_package_json(version):
    if not PACKAGE_JSON.exists():
        print(f"Error: package.json not found at {PACKAGE_JSON}")
        sys.exit(1)
    content = PACKAGE_JSON.read_text()
    if not PACKAGE_VERSION_RE.search(content):
        print(f'Error: no "version" field found in {PACKAGE_JSON}')
        sys.exit(1)
    new_content = PACKAGE_VERSION_RE.sub(rf'\g<1>{version}\g<2>', content, count=1)
    if new_content != content:
        # Validate the result still parses as JSON before writing.
        json.loads(new_content)
        PACKAGE_JSON.write_text(new_content)
        print(f'Updated {PACKAGE_JSON.relative_to(REPO_ROOT)} -> {version}')
    else:
        print(f'{PACKAGE_JSON.relative_to(REPO_ROOT)} already at {version}')


def check(version):
    ok = True
    cfg = config_current_version(CONFIG_H.read_text()) if CONFIG_H.exists() else None
    pkg = package_current_version(PACKAGE_JSON.read_text()) if PACKAGE_JSON.exists() else None

    if cfg != version:
        print(f'MISMATCH config.h FIRMWARE: "FreeButton {cfg}" != VERSION {version}')
        ok = False
    else:
        print(f'OK config.h FIRMWARE: FreeButton {version}')

    if pkg != version:
        print(f'MISMATCH web/package.json version: "{pkg}" != VERSION {version}')
        ok = False
    else:
        print(f'OK web/package.json version: {version}')

    return ok


def main():
    args = sys.argv[1:]

    if args and args[0] == "--check":
        version = read_version_file()
        sys.exit(0 if check(version) else 1)

    if args:
        version = normalize(args[0])
        write_version_file(version)
        print(f'Wrote VERSION -> {version}')
    else:
        version = read_version_file()

    sync_config_h(version)
    sync_package_json(version)
    print(f'Synced all locations to {version}')


if __name__ == "__main__":
    main()
