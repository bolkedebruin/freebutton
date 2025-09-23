#!/usr/bin/env python3
"""
Build information script for FreeButton firmware
Generates build metadata and build flags
"""

import os
import sys
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

def get_git_info():
    """Get git commit information"""
    try:
        # Get short commit hash
        commit_hash = subprocess.check_output(
            ['git', 'rev-parse', '--short', 'HEAD'],
            stderr=subprocess.DEVNULL
        ).decode().strip()

        # Get branch name
        branch = subprocess.check_output(
            ['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
            stderr=subprocess.DEVNULL
        ).decode().strip()

        # Check if working directory is clean
        is_dirty = subprocess.call(
            ['git', 'diff', '--quiet', 'HEAD'],
            stderr=subprocess.DEVNULL
        ) != 0

        return {
            'commit': commit_hash,
            'branch': branch,
            'dirty': is_dirty
        }
    except:
        return {
            'commit': 'unknown',
            'branch': 'unknown',
            'dirty': False
        }

def get_build_info():
    """Generate build information"""

    git_info = get_git_info()

    build_info = {
        'timestamp': datetime.now(timezone.utc).isoformat(),
        'git': git_info,
        'platform': os.environ.get('PIOPLATFORM', 'local'),
        'environment': os.environ.get('PIOENV', 'unknown')
    }

    return build_info

def generate_build_flags():
    """Generate build flags for PlatformIO"""

    git_info = get_git_info()
    timestamp = datetime.now(timezone.utc).strftime('%Y%m%d%H%M%S')

    flags = []
    flags.append(f'-D BUILD_TIMESTAMP="{timestamp}"')
    flags.append(f'-D GIT_COMMIT="{git_info["commit"]}"')
    flags.append(f'-D GIT_BRANCH="{git_info["branch"]}"')

    if git_info['dirty']:
        flags.append('-D BUILD_DIRTY=1')

    # Add CI environment flag if running in CI
    if os.environ.get('CI'):
        flags.append('-D BUILD_CI=1')

    return flags

def main():
    if len(sys.argv) < 2:
        print("Usage: python build_info.py <command>")
        print("Commands:")
        print("  info    - Print build information as JSON")
        print("  flags   - Print build flags for PlatformIO")
        sys.exit(1)

    command = sys.argv[1]

    if command == "info":
        build_info = get_build_info()
        print(json.dumps(build_info, indent=2))
    elif command == "flags":
        flags = generate_build_flags()
        print(' '.join(flags))
    else:
        print(f"Unknown command: {command}")
        sys.exit(1)

if __name__ == "__main__":
    main()