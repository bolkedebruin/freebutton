# FreeButton Build Tools

This directory contains build automation tools and scripts for the FreeButton project.

## Directory Structure

```
tools/
├── fonts/               # UI font build (dev only; outputs committed in media/fonts/)
│   ├── build_fonts.py   # Montserrat + MDI sources -> media/fonts/*.ttf
│   ├── check_fonts.py   # coverage + kerning check, as the device reads them
│   ├── requirements.txt # fontTools (pinned)
│   ├── mdi_glyphs.txt   # curated icon list (--curated build, coverage check)
│   └── *.ttf            # source fonts (Montserrat + Material Design Icons)
├── scripts/            # Build and utility scripts
│   ├── sync_version.py      # Version management (VERSION -> config.h + web/package.json)
│   ├── build_info.py        # Build metadata generation
│   ├── assemble_littlefs.py # Stages the LittleFS image (config, examples, gzipped web IDE)
│   ├── ota_fs.py            # LittleFS image OTA keeping the user's files; backups, --restore
│   ├── media_sync.py        # push / pull / ls of the media bucket `fonts` (S3, SigV4)
│   └── fb_sigv4.py          # SigV4 signing for the device API (library + CLI)
└── partition_tables/
    └── button_v2.csv    # 16 MB layout: docs/MEDIA-STORE.md, migration: docs/MIGRATION-PARTITIONS-V2.md
```

## Fonts

The device renders TrueType fonts with LVGL tiny_ttf from the media store
(bucket `fonts`). The defaults are committed in `media/fonts/` (see its
README for sources, licenses and what the build changes), so a normal build
does not touch fonts. Rebuild them only when the sources, coverage or symbol
mapping change:

```bash
python3 -m venv /tmp/fbfonts
/tmp/fbfonts/bin/pip install -r tools/fonts/requirements.txt
/tmp/fbfonts/bin/python tools/fonts/build_fonts.py   # writes media/fonts/*.ttf
/tmp/fbfonts/bin/python tools/fonts/check_fonts.py   # must pass before committing
```

Both scripts work offline. `check_fonts.py` also scans the repo (configs,
device scripts, web IDE, firmware, tests) for MDI codepoints and fails when
`icons.ttf` lacks one. Sizes are chosen at runtime (`src/ui/fonts/fonts.c`),
not in the font files.

## Version Management

The single source of truth is the repo-root `VERSION` file. `sync_version.py`
propagates it into `include/config.h` (the `FIRMWARE` define, preserving the
`FreeButton ` prefix) and `web/package.json`.

### Get Current Version

```bash
cat VERSION
```

### Update Version

```bash
# Override VERSION and sync all locations
python3 tools/scripts/sync_version.py "3.2.0"

# Re-sync from the current VERSION file (no override)
python3 tools/scripts/sync_version.py

# Verify everything is in lockstep (non-zero exit on drift)
python3 tools/scripts/sync_version.py --check
```

## Build Information

Generate build metadata and flags:

```bash
# Get build information as JSON
python3 tools/scripts/build_info.py info

# Get build flags for PlatformIO
python3 tools/scripts/build_info.py flags
```

## GitHub Actions Integration

The build pipeline automatically:

1. **CI Pipeline** (`.github/workflows/ci.yml`):
   - Triggered on push/PR to main/develop
   - Checks out the mquickjs submodule
   - Builds and tests the web IDE, checks generated artifacts for engine drift
   - Builds firmware for ESP32 and the LittleFS image
   - Compile-checks the native environment
   - Checks firmware.bin against app0 and littlefs.bin against the `data` partition
   - Uploads build artifacts (firmware.bin, firmware.elf, bootloader.bin, partitions.bin, littlefs.bin)
   - Fonts and wasm assets are committed, so no font/emcc generation runs

2. **Release Pipeline** (`.github/workflows/release.yml`):
   - Triggered on GitHub releases or manual dispatch
   - Syncs the firmware version via `sync_version.py`
   - Builds the web IDE, firmware, and the LittleFS image
   - Packages firmware.bin, firmware.elf, bootloader.bin, partitions.bin, littlefs.bin
     and FLASHING.txt (flash offsets) together
   - Uploads release assets

## Local Development

### Prerequisites

- Python 3.12+
- Node.js 20+
- PlatformIO Core
- fontTools (only to rebuild the fonts, see above)

### Setup

```bash
# Install PlatformIO
pip install platformio

# Build firmware
pio run
```

### Testing

```bash
# Run native tests
pio test -e native

# Build and test
pio run -e native && pio test -e native
```

## Release Process

1. **Create a GitHub Release**:
   - Tag with semantic version (e.g., `v3.2.0`)
   - GitHub Actions will automatically build and attach firmware

2. **Manual Release** (if needed):
   - Go to Actions → "Build and Release"
   - Click "Run workflow"
   - Enter version string

The release artifacts include:
- `freebutton_X.Y.Z_firmware.tar.gz` - Complete firmware package
- Contains: firmware.bin, firmware.elf (for core dumps), bootloader.bin,
  partitions.bin, littlefs.bin (the LittleFS image bundling the web IDE,
  config and examples) and FLASHING.txt. The fonts live in the `media`
  partition, not in littlefs.bin: upload them with
  `python3 tools/scripts/media_sync.py push <device URL>`.

## Flashing Firmware

Use ESP-IDF tools or PlatformIO to flash the firmware:

```bash
# Using PlatformIO
pio run -e esp32 -t upload     # bootloader, partition table, otadata, app
pio run -e esp32 -t uploadfs   # LittleFS image (web IDE, config, examples)

# Using ESP-IDF
idf.py flash
```

Requires ESP-IDF tools to be installed and configured. A device still on
partition table v1 needs a full erase first: see
[docs/MIGRATION-PARTITIONS-V2.md](../docs/MIGRATION-PARTITIONS-V2.md).