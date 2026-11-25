# FreeButton Build Tools

This directory contains build automation tools and scripts for the FreeButton project.

## Directory Structure

```
tools/
├── fonts/               # upstream source fonts (Montserrat + Material Design Icons);
│   └── *.ttf            #   media/fonts/ holds unmodified copies, Montserrat-Regular is a test font
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
(bucket `fonts`). The defaults in `media/fonts/` are the upstream files
from `tools/fonts/`, unmodified (see its README for sources, checksums and
licenses); there is no font build step. Sizes are chosen at runtime
(`src/ui/fonts/fonts.c`), not in the font files. To change a default, copy
the new upstream TTF over the file in `media/fonts/`, update its README and
run `pio test -e native -f test_ttf_check`.

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
   - Runs the native tests
   - Checks firmware.bin against app0 and littlefs.bin against the `data` partition
   - Uploads build artifacts (firmware.bin, firmware.elf, bootloader.bin, partitions.bin, littlefs.bin)
   - Fonts and wasm assets are committed, so no font/emcc generation runs

2. **Release Pipeline** (`.github/workflows/release.yml`):
   - Runs when a tag `v<version>` is pushed (`git tag v3.1.0 && git push github v3.1.0`);
     the tag must match `VERSION`, or the run fails
   - Manual dispatch builds the package without a release
   - Builds the web IDE, firmware, and the LittleFS image
   - Packages firmware.bin, firmware.elf, bootloader.bin, partitions.bin, littlefs.bin
     and FLASHING.txt (flash offsets) together
   - Creates the GitHub release with the package (a pre-release when the version has a suffix)

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