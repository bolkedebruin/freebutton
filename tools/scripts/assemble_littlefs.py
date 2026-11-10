#!/usr/bin/env python3
"""Assemble the LittleFS image staging directory (.pio/staging_data).

The firmware serves the web IDE from LittleFS ``/www/`` (gzip-only; see
``src/core/WebServer.cpp`` — serveStatic auto-negotiates ``.gz`` and the SPA
fallback serves ``/www/index.html.gz`` explicitly). This script stages, with
zero external dependencies at runtime:

  * config + data files at the image root (config.json, virt*.json, ...);
    FREEBUTTON_CONFIG=<path> takes config.json from another file
    (e.g. devices/<id>/config.json)
  * the built web bundle from ``web/dist/`` as ``www/<relpath>.gz``
  * device fonts from ``data/fonts/*.bin``, streamed by src/ui/fonts/fonts.c
    (the compiled fonts are the fallback when a file is missing)

Build output is NEVER written back into ``data/``; everything lands in the
throwaway ``.pio/staging_data`` staging dir, which becomes PlatformIO's
``data_dir`` for ``buildfs``.

Runnable standalone (``python3 tools/scripts/assemble_littlefs.py``) and also
auto-invoked as a PlatformIO ``pre:`` extra-script for filesystem targets
(``buildfs`` / ``uploadfs``) only.
"""

import gzip
import os
import shutil
import sys

# Files copied verbatim from data/ to the image root.
ROOT_FILES = [
    "config.json",
    "config.json.home",
    "config.json.old_style",
    "virt.json",
    "virt2.json",
    "bytecode_upload.html",
]

STAGING_SUBDIR = os.path.join(".pio", "staging_data")


def _gzip_file(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(src, "rb") as f_in:
        data = f_in.read()
    # mtime=0 keeps the image reproducible across builds.
    with gzip.GzipFile(dst, "wb", mtime=0) as f_out:
        f_out.write(data)


def assemble(project_dir):
    data_dir = os.path.join(project_dir, "data")
    dist_dir = os.path.join(project_dir, "web", "dist")
    staging = os.path.join(project_dir, STAGING_SUBDIR)

    # Start from a clean staging dir so removed source files do not linger.
    if os.path.isdir(staging):
        shutil.rmtree(staging)
    os.makedirs(staging, exist_ok=True)

    copied = 0

    # 1. Root config/data files. FREEBUTTON_CONFIG=<path> replaces
    #    data/config.json, e.g. a device's devices/<id>/config.json.
    config_override = os.environ.get("FREEBUTTON_CONFIG")
    if config_override and not os.path.isabs(config_override):
        config_override = os.path.join(project_dir, config_override)
    if config_override and not os.path.isfile(config_override):
        print(f"[assemble_littlefs] ERROR: FREEBUTTON_CONFIG={config_override} not found",
              file=sys.stderr)
        sys.exit(1)
    for name in ROOT_FILES:
        src = os.path.join(data_dir, name)
        if name == "config.json" and config_override:
            src = config_override
            print(f"[assemble_littlefs] config.json <- {src}")
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(staging, name))
            copied += 1
        else:
            print(f"[assemble_littlefs] WARN: missing {src}", file=sys.stderr)

    # 2. Example scripts: data/scripts/examples/*.js -> scripts/examples/
    examples_src = os.path.join(data_dir, "scripts", "examples")
    if os.path.isdir(examples_src):
        examples_dst = os.path.join(staging, "scripts", "examples")
        os.makedirs(examples_dst, exist_ok=True)
        for name in sorted(os.listdir(examples_src)):
            if name.endswith(".js"):
                shutil.copy2(os.path.join(examples_src, name),
                             os.path.join(examples_dst, name))
                copied += 1

    # 3. Device fonts: data/fonts/*.bin -> fonts/
    fonts_src = os.path.join(data_dir, "fonts")
    if os.path.isdir(fonts_src):
        fonts_dst = os.path.join(staging, "fonts")
        os.makedirs(fonts_dst, exist_ok=True)
        for name in sorted(os.listdir(fonts_src)):
            if name.endswith(".bin"):
                shutil.copy2(os.path.join(fonts_src, name),
                             os.path.join(fonts_dst, name))
                copied += 1

    # 4. Web bundle: gzip every file in web/dist/ into www/<relpath>.gz
    www_files = 0
    if os.path.isdir(dist_dir):
        for root, _dirs, files in os.walk(dist_dir):
            for name in files:
                src = os.path.join(root, name)
                rel = os.path.relpath(src, dist_dir)
                dst = os.path.join(staging, "www", rel + ".gz")
                _gzip_file(src, dst)
                www_files += 1
    else:
        print(f"[assemble_littlefs] WARN: web/dist not found at {dist_dir}; "
              "run `cd web && npm run build` first", file=sys.stderr)

    # Report total staged size vs the 4MB `data` partition.
    total = 0
    for root, _dirs, files in os.walk(staging):
        for name in files:
            total += os.path.getsize(os.path.join(root, name))
    print(f"[assemble_littlefs] staged {copied} data files + {www_files} "
          f"gzipped www/ files ({total} bytes) into {staging}")
    return staging


def _project_dir():
    env_dir = os.environ.get("PROJECT_DIR")
    if env_dir:
        return env_dir
    # tools/scripts/assemble_littlefs.py -> project root is two levels up.
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


# PlatformIO extra-script hook. `Import` is a SCons builtin injected only when
# this file is exec'd as an extra_script, so referencing it distinguishes the
# PlatformIO context from a plain standalone run without depending on __name__.
try:
    Import("env")  # noqa: F821
    _UNDER_PIO = True
except NameError:
    _UNDER_PIO = False

if _UNDER_PIO:
    from SCons.Script import COMMAND_LINE_TARGETS  # type: ignore
    if any(t in ("buildfs", "uploadfs") for t in COMMAND_LINE_TARGETS):
        assemble(env.subst("$PROJECT_DIR"))  # noqa: F821

if __name__ == "__main__" and not _UNDER_PIO:
    assemble(_project_dir())
