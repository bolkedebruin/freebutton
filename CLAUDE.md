# OpenWolf

This project uses OpenWolf for context management. The always-on rules live in `.claude/rules/openwolf.md`; the hooks handle bookkeeping (anatomy index, memory log, read tracking) automatically.

For the full operating protocol (session handoff, memory discipline, bug logging), load the `openwolf` skill, or read `.wolf/OPENWOLF.md`. Regenerate the session handoff with `/handoff`.


# CLAUDE.md

Guidance for Claude Code (claude.ai/code) in this repository.

FreeButton is firmware for an ESP32-S3 smart button (16 MB flash, no PSRAM)
and a web IDE that the device serves. Version: `VERSION` (alpha).

## Commands

```bash
# Firmware (PlatformIO Core 6.2+, platform pioarduino 55.03.313:
# Arduino 3.3 as a component of ESP-IDF 5.5)
pio run -e esp32                 # build
pio run -e esp32 -t buildfs      # LittleFS image: web IDE, data/config.json, examples
pio run -e esp32 -t upload       # USB: bootloader, partition table, firmware
pio run -e esp32 -t uploadfs     # USB: LittleFS image
pio test -e native               # firmware logic on the host (Unity)
pio test -e native -f test_auth  # one suite

# Web IDE (Node.js 20)
cd web && npm ci
npm run build                    # prebuild regenerates the script API files
npm test                         # vitest: IDE and the virtual device
npm run dev                      # vite dev server

# Version: VERSION is the source; never edit the copies by hand
python3 tools/scripts/sync_version.py "3.2.0"
python3 tools/scripts/sync_version.py --check
```

CI (`.github/workflows/ci.yml`) runs: wasm/stdlib fingerprint check, web
build, a diff check of the generated web files, web tests, firmware build,
`buildfs`, a size check against the partition table, native tests.

## Layout

| Path | Content |
| --- | --- |
| `src/main.cpp` | start-up order, task and heap budget |
| `src/core/` | WiFi and setup portal, web server, S3-style file API (`S3Server`), SigV4 authentication (`Auth`), static files, NTP, mDNS, LittleFS |
| `src/components/` | what `config.json` describes: buttons, display items, lists, images, clock, names, topics; `ConfigManager` loads it |
| `src/drivers/` | displays (`DisplayManager`, `LvglDisplay`), keys, LEDs, sensors, hardware scan, device info |
| `src/ui/` | LVGL widgets in C (`ui_*.c`), styles, text spans, modals, the main page |
| `src/mqtt/` | brokers (`Registry`, `Broker`), subscribers and publishers (`Component`), Home Assistant discovery |
| `src/scripting/` | the JavaScript engine (mquickjs): `ScriptEngine`, bindings (`*_binding.cpp`, `js/freebutton_*.c`), compiled-script store (`Bytecode*`) |
| `src/media/` | media store: fonts and images in their own partition |
| `src/json/` | SAX reading (`FilteredReaderHandler`) and streaming writing (`JsonWriter`) |
| `lib/mquickjs` | the engine (git submodule); `/sync-mquickjs` updates it |
| `data/` | default `config.json` and example scripts of a new device |
| `media/fonts` | default fonts (Montserrat, Material Design Icons), unmodified upstream files |
| `test/` | native test suites, one folder each |
| `tools/scripts/` | build steps (`pre_build_js_stdlib.py`, `build_info_flags.py`, `assemble_littlefs.py`) and device tools (`ota_fs.py`, `media_sync.py`, `fb_sigv4.py`, `screenshot.py`, `flash_media.py`) |
| `tools/js/` | wasm builds: the script compiler and the emulator (`build_emulator_wasm.sh`) |
| `tools/partition_tables/button_v2.csv` | nvs, app0/app1 (3 MB each), coredump, `js_user` (compiled scripts), `media` (5 MB), `data` (LittleFS) |
| `web/src/components/` | the IDE (Lit elements `fb-*`, CodeMirror 6, Tailwind) |
| `web/src/js/` | IDE logic; `emulator/` runs the firmware's UI and script engine as wasm (the virtual device) |
| `devices/` | git-ignored: the files of real devices, a repository of the owner (`docs/DEVICES.md`) |

Documentation is in `docs/` (index in `README.md`): names, styles, lists,
emulator, authentication, script store, media store, partition migration.

## Rules

**Memory.** The heap is very tight (tens of KB free at idle, a few KB at the
worst moment).
- Do not copy documents, strings or structs; work in place and generate
  seldom-used data on demand.
- No `std::string` or `std::function` in new code paths; fixed buffers and
  `std::string_view`.
- Never parse JSON into a RapidJSON `Document` (DOM): SAX only, with
  `FilteredReaderHandler`.
- The script engine is the main way a device gets its behaviour: its static
  buffers stay.

**No C++ exceptions.** The firmware builds with `-fno-exceptions`
(`-std=gnu++23`). Use return values, `std::optional` or error enums.

**Logic that can run on the host goes in a header** (`*Logic.h`, `*_logic.c`)
with a suite in `test/`; the ESP32 part stays thin.

**Generated files, never edited by hand:** `web/src/js/freebutton.d.ts`,
`web/src/js/autocomplete-data.ts`, `web/src/js/stdlib.json` (from the
`@jsapi` comments in `src/scripting/js/*.c`, by `npm run build`), and
`src/scripting/generated/`.

**Config format.** Only FreeButton's own `config.json` format is read
(`docs/NAMES.md`, `docs/STYLE.md`). The format of the original firmware is
not: a key of it is reported once at load and ignored.

**HTTP API.** No TLS on the device; requests are signed with AWS Signature
Version 4 (`docs/AUTH.md`, client: `tools/scripts/fb_sigv4.py`). Credentials
are never logged, printed or written to the repository.

**Settings that depend on the bootloader** (`sdkconfig`: flash mode, cache,
partition table) are tested over USB on a bench device first, never sent
over the network to a device in use.

**Nothing of one installation in this repository:** no real device ids,
addresses, host names or topic IDs. Examples use `192.168.1.x` and the
placeholder ids `btn_a00001` to `btn_a00003`; tests that need real device
files read them from `devices/` (`docs/DEVICES.md`).
