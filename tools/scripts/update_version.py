#!/usr/bin/env python3
"""
Version management script for FreeButton firmware
Updates the firmware version in config.h while preserving format
"""

import sys
import re
import os
from pathlib import Path

def update_version(version_string):
    """Update the firmware version in config.h"""

    # Path to config.h
    config_path = Path(__file__).parent.parent.parent / "include" / "config.h"

    if not config_path.exists():
        print(f"Error: config.h not found at {config_path}")
        sys.exit(1)

    # Read the current content
    content = config_path.read_text()

    # Pattern to match the FIRMWARE define
    pattern = r'(#define\s+FIRMWARE\s+")[^"]*(")'

    # Replace with new version
    new_content = re.sub(pattern, rf'\g<1>{version_string}\g<2>', content)

    if new_content == content:
        print(f"Warning: No FIRMWARE define found in {config_path}")
        return False

    # Write back to file
    config_path.write_text(new_content)
    print(f"Updated firmware version to: {version_string}")
    return True

def get_current_version():
    """Get the current firmware version from config.h"""

    config_path = Path(__file__).parent.parent.parent / "include" / "config.h"

    if not config_path.exists():
        return None

    content = config_path.read_text()

    # Pattern to extract the current version
    pattern = r'#define\s+FIRMWARE\s+"([^"]*)"'
    match = re.search(pattern, content)

    if match:
        return match.group(1)

    return None

def main():
    if len(sys.argv) < 2:
        print("Usage: python update_version.py <version>")
        print("       python update_version.py --get")
        sys.exit(1)

    if sys.argv[1] == "--get":
        version = get_current_version()
        if version:
            print(version)
        else:
            print("Error: Could not extract version from config.h")
            sys.exit(1)
    else:
        version = sys.argv[1]

        # Clean version string (remove 'v' prefix if present)
        if version.startswith('v'):
            version = version[1:]

        if update_version(version):
            print(f"Successfully updated version to {version}")
        else:
            print("Failed to update version")
            sys.exit(1)

if __name__ == "__main__":
    main()