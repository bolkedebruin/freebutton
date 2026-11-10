# Moving a device to partition table v2

Partition table v2 (`tools/partition_tables/button_v2.csv`, layout and
rationale in [MEDIA-STORE.md](MEDIA-STORE.md)) replaces `button_v1.csv`:

| Partition | v1 | v2 |
|---|---|---|
| `phy_init` | 0x9000, 4 KB | removed (PHY init data is compiled in: `CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION` is off) |
| `nvs` | 0x710000, 16 KB | 0x9000, 20 KB |
| `otadata` | 0xA000, 8 KB | 0xE000, 8 KB |
| `app0` / `app1` | 0x10000 / 0x390000, 3.5 MB each | 0x10000 / 0x310000, 3 MB each |
| `coredump` | - | 0x610000, 64 KB (new: core dump to flash) |
| `js_user` | 0x714000, 256 KB | 0x620000, 256 KB |
| `img_data` (unused) | 0x754000, 1 MB | replaced by `media`, 0x660000, 5 MB |
| `data` (LittleFS) | 0x854000, 4 MB | 0xB60000, 4.6 MB (0x4A0000) |

Almost every partition moves, so a v2 firmware cannot be installed over the
air on a v1 device: the move is a serial flash with a **full erase, once per
device**. The erase loses:

- NVS: the WiFi credentials (namespace `wifi`) and the device access
  credentials (namespace `fbauth`, see [AUTH.md](AUTH.md)). New access
  credentials are generated at the first boot.
- `js_user`: the compiled scripts (`main.jsc`).
- LittleFS: `config.json`, `scripts/`, the bytecode index
  `js_bytecode_meta.json` and every other user file.
- The fonts: v1 kept them on LittleFS (`/fonts/*.bin`); v2 reads TrueType
  fonts from the `media` partition (bucket `fonts`).

The device name (`btn_xxxxxx`) comes from the MAC address and stays the same.

The commands below use `~/.local/bin/pio` as `pio` and run from the
repository root. `btn_a00001` and `192.168.1.50` stand for your device and its
address.

## 1. Back up (v1 firmware, over WiFi)

Build the web IDE first (`cd web && npm ci && npm run build`); `ota_fs.py`
builds an image as part of the backup.

```sh
export FB_CREDENTIALS='FBCRED:<akid>:<secret>'   # hold the factory button 6 s to show them
python3 tools/scripts/ota_fs.py http://192.168.1.50 --dry-run
```

`--dry-run` backs up and builds an image but uploads nothing. The backup is
in `devices/btn_a00001/backup/fs-<timestamp>/` (git-ignored):

- `app/`: every object of the bucket `app`: `config.json`, `scripts/`
  (including `scripts/main.js`), `js_bytecode_meta.json`, other user files.
- `fonts/`: the bucket `fonts`. On a v1 device these are the old `.bin`
  fonts, which v2 does not use; you do not need to restore them.

Check that `app/config.json` and `app/scripts/main.js` are there before you
continue. Also keep a copy of the WiFi name and password: the erase deletes
them from the device.

## 2. Erase and flash (serial, USB)

```sh
pio run -e esp32 -t erase      # whole flash: NVS, js_user, LittleFS, everything
pio run -e esp32 -t upload     # bootloader, partition table v2, otadata, app0
pio run -e esp32 -t uploadfs   # LittleFS image: web IDE, default config.json, examples
```

`upload` writes `bootloader.bin` at 0x0, `partitions.bin` at 0x8000,
`boot_app0.bin` at 0xE000 (the otadata offset of the table) and
`firmware.bin` at 0x10000. `uploadfs` writes `littlefs.bin` at 0xB60000.
`FREEBUTTON_CONFIG=devices/btn_a00001/config.json pio run -e esp32 -t uploadfs`
puts that `config.json` into the image instead of `data/config.json`.

Optional, also over serial: write the default fonts into the `media`
partition with `tools/scripts/flash_media.py` (builds a media partition image
from `media/<bucket>/<key>` and writes it with esptool; see its `--help`).
Then step 5 is not needed.

## 3. WiFi and access credentials

The device boots without WiFi credentials and starts the configuration
portal (`src/core/WifiManager.cpp`):

1. The display shows a QR code with the access point: SSID `FreeButton`,
   password 8 characters derived from the MAC address (also shown on the
   display, and logged on the serial console).
2. Connect to that access point. The captive portal opens, or browse to
   `http://192.168.4.1/`.
3. Pick the network (the list comes from `/scan`), enter its password and
   connect. The device stores them in NVS (namespace `wifi`, keys `ssid`,
   `password`) and connects at once, without a reboot; the portal closes.
4. Find the new address (router, or the device info screen: hold the factory
   button 2 s).
5. New access credentials: hold the factory button 6 s. The display shows
   the key id, the secret and a QR code `FBCRED:<akid>:<secret>`. Put them in
   `FB_CREDENTIALS` for the tools and in the IDE (Settings → Device access,
   or open the QR code's link on a phone). The old credentials no longer
   work.

## 4. Restore the user files (over WiFi)

```sh
export FB_CREDENTIALS='FBCRED:<new akid>:<new secret>'
python3 tools/scripts/ota_fs.py http://192.168.1.50 \
    --restore devices/btn_a00001/backup/fs-<timestamp>/app
```

This builds a LittleFS image from the current build (web IDE, examples) with
the backed-up files on top, uploads it and reboots the device (it backs up
the fresh device first, into a new `fs-<timestamp>`). `--restore` leaves out
`js_bytecode_meta.json`: the bytecode it indexes was in the erased
`js_user`, so the device starts with an empty registry. Add
`--config devices/btn_a00001/config.json` to use that file instead of the
backed-up `config.json`.

`config.json` holds the MQTT brokers, buttons, displays and the rest of the
device configuration, so the device works as before after this reboot
(except the scripts, step 6).

## 5. Fonts (over WiFi)

Skip this step if you used `flash_media.py` in step 2.

```sh
python3 tools/scripts/media_sync.py push http://192.168.1.50   # media/fonts/* -> bucket fonts
python3 tools/scripts/media_sync.py ls   http://192.168.1.50   # sizes, ETags, store usage
```

`push` skips objects whose ETag equals the local file's MD5, so it can be run
again. Custom fonts: `push --dir <directory>`. Until the fonts are there, the
UI uses LVGL's built-in Montserrat 14 (no icons).

## 6. Compiled scripts

The bytecode was in `js_user` and is gone. Scripts are compiled in the
browser by the web IDE (mquickjs compiled to wasm, `web/public/assets/`);
there is no command-line uploader. `tools/js/` only builds that wasm
compiler and checks that it matches the firmware's stdlib.

1. Open the IDE on the device: `http://192.168.1.50/`.
2. Settings → Device access: the new credentials (step 3).
3. Open `scripts/main.js` (restored in step 4).
4. Deploy (toolbar: "Deploy: save, compile main.js and upload as main.jsc").
   The IDE saves the source, asks the device where the bytecode will go
   (`GET /api/bytecode/next_address`), compiles it relocated for that
   address and uploads it as `main.jsc` (`POST /api/bytecode/upload`,
   signed). The device runs `main.jsc` at boot; only `scripts/main.js` is
   compiled, other `.js` files are only stored.

## 7. Verify

- `GET http://192.168.1.50/api/version`: the new firmware version.
- The serial log at boot (`pio run -e esp32 -t monitor`): `Found js_user
  partition at 0x620000`, LittleFS mounted, WiFi connected.
- `python3 tools/scripts/media_sync.py ls http://192.168.1.50`: the fonts, and
  the `x-fb-media-*` usage.
- `GET /api/bytecode/list`: `main.jsc`, not stale.
- The display shows the configured pages with the TrueType fonts and icons;
  buttons and MQTT work; the script runs.
- `pio run -e esp32 -t coredump` (step below) reports no core dump.

## Core dumps

v2 firmware writes a core dump to the `coredump` partition (0x610000, 64 KB)
when it panics: ELF format, CRC32, the registers and stacks of the tasks (no
heap or `.bss`). A new crash overwrites the previous dump. Cost, measured
on the build: about 2.3 KB of internal RAM (DRAM `.data` +1.3 KB, `.bss`
+0.8 KB, mostly the ISR stacks growing from 1536 to 2096 bytes, which the
ELF format needs; IRAM +0.25 KB) and 7.5 KB of app flash. The core dump log
strings are off (`CONFIG_ESP_COREDUMP_LOGS=n`, ~5 KB DRAM saved), so the
boot log does not announce a stored dump. To read it, with
the device on USB and the `firmware.elf` of the exact build that crashed
(`.pio/build/esp32/firmware.elf`, also in the CI artefacts):

```sh
pio run -e esp32 -t coredump-only            # reads the partition over serial and prints the summary
pio run -e esp32 -t coredump-only -- --chip esp32s3 --port /dev/cu.usbmodem1101 \
    info_corefile .pio/build/esp32/firmware.elf
```

`coredump` instead of `coredump-only` builds the firmware first; only use it
when the build tree is the one that is on the device. Offline, from a copy of
the partition:

```sh
esptool.py --chip esp32s3 read_flash 0x610000 0x10000 coredump.bin
python -m esp_coredump --chip esp32s3 info_corefile --core coredump.bin --core-format raw \
    .pio/build/esp32/firmware.elf
```

(`esp_coredump` is installed in PlatformIO's Python, `~/.platformio/penv/bin/python`;
a `coredump.bin` in the project root is picked up by `-t coredump-only` without
arguments.) `dbg_corefile` instead of `info_corefile` opens GDB on the dump.
The partition is not cleared after reading; `esptool.py erase_region 0x610000
0x10000` clears it.
