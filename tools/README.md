# FreeButton Build Tools

This directory contains build automation tools and scripts for the FreeButton project.

## Directory Structure

```
tools/
├── fonts/               # Font generation and management
│   ├── generatefonts.sh # Main font generation script
│   ├── get_mdi_codes.sh # Material Design Icons code extractor
│   └── *.ttf           # Font files (Montserrat + Material Design Icons)
├── scripts/            # Build and utility scripts
│   ├── update_version.py   # Version management
│   └── build_info.py      # Build metadata generation
└── partition_tables/   # ESP32 partition table definitions
```

## Font Generation

The font generation system creates LVGL-compatible fonts with Material Design Icons:

### Usage

```bash
cd tools/fonts
./generatefonts.sh
```

This generates:
- Multiple font sizes (14, 18, 20, 24, 32, 48, 72)
- Both C source files (.c) and binary files (.bin)
- Extended Unicode symbols for international support
- Material Design Icons integration

### Customization

Edit `generatefonts.sh` to:
- Add/remove font sizes: Modify `FONT_SIZES` array
- Add/remove symbols: Modify `USER_SYMBOLS` and `LARGE_SYMBOLS`
- Add/remove icons: Modify `ICON_NAMES` array

## Version Management

### Get Current Version

```bash
python3 tools/scripts/update_version.py --get
```

### Update Version

```bash
python3 tools/scripts/update_version.py "3.2.0"
```

The script automatically updates the `FIRMWARE` define in `include/config.h`.

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
   - Generates fonts
   - Builds firmware for ESP32
   - Runs native tests
   - Uploads build artifacts

2. **Release Pipeline** (`.github/workflows/release.yml`):
   - Triggered on GitHub releases or manual dispatch
   - Updates firmware version automatically
   - Generates fonts
   - Builds release firmware
   - Creates firmware packages with flash scripts
   - Uploads release assets

## Local Development

### Prerequisites

- Python 3.11+
- Node.js 20+
- PlatformIO Core
- lv_font_conv (npm package)
- jq (for icon processing)

### Setup

```bash
# Install PlatformIO
pip install platformio

# Install font converter
npm install -g lv_font_conv

# Generate fonts (first time)
cd tools/fonts && ./generatefonts.sh

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
- Contains: firmware.bin, bootloader.bin, partitions.bin

## Flashing Firmware

Use ESP-IDF tools or PlatformIO to flash the firmware:

```bash
# Using PlatformIO
pio run -e esp32 -t upload

# Using ESP-IDF
idf.py flash
```

Requires ESP-IDF tools to be installed and configured.