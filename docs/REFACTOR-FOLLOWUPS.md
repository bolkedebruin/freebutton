# Build/Release Refactor — Follow-ups & Handoff

Companion to the completed effort on branch `rework/build-release-sync`.
Full plan: `~/.claude/plans/abundant-nibbling-sundae.md`. Learnings: `.wolf/cerebrum.md`, bugs: `.wolf/buglog.json`.

---

## 1. WS-G — Streaming-font port to LVGL 9.5 (deferred, hardware-gated)

**Why deferred:** needs LVGL-internals work AND on-device verification (rendering + RAM) that can't be done headless. The compiled-in fonts remain the LIVE render path and work today; the `.bin` assets are already staged in the LittleFS image (`data/fonts/*.bin` → `/fonts/`) ready for the switch.

**The work:** `src/ui/fonts/fonts.c` has a custom streaming loader (`streaming_font_init` + a ported `lv_binfont_loader` + per-glyph cache) that reads `S:/fonts/mnt_*.bin` on demand. It was ported for **LVGL 9.3** and is broken on **9.5** — it compiles but uses the 9.3 glyph API (`get_glyph_bitmap(lv_font_glyph_dsc_t*, lv_draw_buf_t*)`, `g_dsc->gid.index`, `g_dsc->resolved_font`) whose semantics changed in 9.4–9.5. Re-port `streaming_font_get_glyph_dsc` / `streaming_font_get_glyph_bitmap` + the ported static helpers against current `lv_binfont_loader.c` / `lv_font_fmt_txt.c` + the 9.5 glyph-cache API. Structure is intact — field/callback work, not a rewrite. **Public API (`lv_binfont_create`) is NOT an option: no PSRAM (~512KB SRAM), and it loads whole fonts into RAM (335KB for the 72px face).** LVGL is pinned to exactly `9.5.0` (WS-D) precisely because this binds to internals — re-verify on any bump.

**Then:** once streaming renders correctly on hardware, drop the bulk compiled `.c` (reclaims ~1MB app flash; currently 86.4%). **Fallback is a separate deferred decision** — see the `font-fallback-strategy` memory: likely keep a standard base font compiled in and use LVGL's `lv_font_t.fallback` chain so user-uploaded filesystem fonts extend it for MDI/special glyphs. Fonts also become uploadable via the web IDE at that point.

**Verify (on device):** `S:/fonts/mnt_*.bin` load succeeds (no "Loading built-in" warnings), text + MDI icons render at every size, RAM bounded (`streaming_font_init` mem logs), app0 drops ~1MB after the `.c` are removed.

---

## 2. On-device verification checklist (can't be done headless)

Flash both images, then verify from a browser:

```bash
# build (fresh)
cd web && npm ci && npm run build && cd ..
~/.local/bin/pio run -e esp32                 # firmware  (retry once if crt.S race, see §4)
~/.local/bin/pio run -e esp32 -t buildfs      # littlefs.bin (web IDE + config + fonts)
~/.local/bin/pio run -e esp32 -t upload       # flash firmware
~/.local/bin/pio run -e esp32 -t uploadfs     # flash filesystem
```

Then browse `http://freebutton.local/` (or the device IP):
- [ ] IDE loads from the device. **DevTools → Network: ZERO external requests** (no button.plus, no fonts.googleapis.com). This is the core "zero external deps" acceptance test.
- [ ] Self-hosted DM Sans / Fira Sans render (served as `.woff2` under `/www/fonts`, gzipped).
- [ ] `mquickjs.wasm` instantiates (served `application/wasm` + gzip) — compile a script in the editor.
- [ ] `GET /api/version` returns `{version, git_commit, dirty, model}` matching `include/config.h`.
- [ ] **Compile → deploy** to the device (the newly-wired Upload/Deploy button): round-trips `/api/bytecode/next_address` → `/api/bytecode/upload`, script runs on device.
- [ ] SPA deep-links / refresh serve `index.html` (onNotFound fallback).

---

## 3. Partition migration (BEFORE shipping an OTA)

WS-E1 grew the `data` LittleFS partition **1MB → 4MB** (`tools/partition_tables/button_v1.csv`). An OTA that changes the partition table **reformats/relocates the filesystem** on already-deployed devices → on-device config + user JS scripts are lost. Options before shipping:
- One-time full reflash (erase + flash firmware + fs) rather than OTA for this version, OR
- A migration path: back up `/` (config.json, user scripts) before the partition change and restore after, OR
- Stage the partition growth so existing user data is preserved.
Decide and document per your deployment/fleet situation. New devices are unaffected.

---

## 4. `crt.S` clean-build race (bug-004) — FIXED

Full-clean builds used to **intermittently** fail with `Source .../rmaker_*.crt.S not found` (~50% of clean builds). Root cause: `esp_insights`/`esp_rainmaker` (hard-required transitive deps of the Arduino-ESP32 framework — `EXCLUDE_COMPONENTS` cannot drop them, verified) embed TLS certs as generated `.crt.S`, and SCons raced the IDF ninja generate step.

**Fix (commit `6eebfd3`):** restore `board_build.embed_txtfiles` with the 4 cert paths — the original workaround that WS-D had wrongly removed. It makes PlatformIO materialize the `.crt.S` embeds up front, so SCons never races ninja. Verified with two consecutive full-clean green builds. The CI `pio run -e esp32 || pio run -e esp32` retry is now just belt-and-suspenders and can be dropped.

Unrelated remaining nit: the `sdkconfig` "defined as an output multiple times" ninja warning (from the `-DSDKCONFIG=${PROJECT_BUILD_DIR}/sdkconfig` override) is non-fatal on full-clean builds; only edit-then-incremental needs a clean. Minor cleanup candidate, not a blocker.

---

## 5. Commit plan (branch `rework/build-release-sync`)

Refactor committed in workstream chunks; **pre-existing uncommitted work is left untouched** (HardwareInitializer, ButtonManager, DisplayManager, PushButton, driver changes, `CLAUDE.md` hardware edits) and so is the `.wolf/`/`.claude/` infra — those are yours to handle.
1. mquickjs submodule + engine/firmware split (`.gitmodules`, `lib/mquickjs`, `src/scripting/js/*`, `tools/js/*` minus wasm, generate scripts, `mquickjs_stdlib.c`)
2. fonts tracked + offline generation (`src/ui/fonts/mnt_*.c`, `generatefonts.sh`, `tools/fonts/mdi_glyphs.txt`, `.gitignore`)
3. version SoT + reproducible deps + build cleanup (`VERSION`, `sync_version.py`, `build_info_flags.py`, `platformio.ini` deps, `sdkconfig.defaults` TLS fix, dead-file deletions)
4. device serves web IDE + wasm (`WebServer.cpp`, `config.h` CDN removal, partition csv, `web/*`, real `web/public/assets/mquickjs.*`, `build_wasm.sh`, `assemble_littlefs.py`, `data/fonts/*`, `data/config.json`, `data/virt.json`) + the Lit upload wiring + validator feature
5. unified CI/release (`.github/workflows/*`, retire `update_version.py`, `tools/README.md`)

---

## 6. TODO — Updating the web IDE without losing user data

**Problem:** the IDE bundle (`www/`), the fonts and the user's files all live in the one LittleFS `data` partition, and the only way to update the IDE today is `uploadfs`, which writes a whole new image. That wipes everything the user created on the device: `scripts/main.js` (and other scripts), `config.json` as edited in the IDE, and `js_bytecode_meta.json` (the bytecode registry index, so `main.jsc` no longer boots). On 2026-11-08 this was worked around by hand (S3 PUT of `scripts/main.js`, bytecode re-upload as `main.jsc`).

**Options:**
1. **Save + restore around `uploadfs`** (cheapest): a PlatformIO pre/post hook or `tools/` script that, before `uploadfs`, downloads every user object through the S3 API (`GET /fs?list-type=2`, skipping `www/` and `fonts/`) and the bytecode names (`/api/bytecode/list`), and afterwards puts them back (S3 PUT, and the bytecode via `/api/bytecode/upload` or a fresh compile of `scripts/main.js`). Works only while the device is on the network; serial-only flashes still lose data.
2. **Update `www/` through the S3 API**: allow writes to `www/` (and `fonts/`) for an authenticated client (needs the SigV4 / shared-key auth that is not there yet), and ship IDE updates as object uploads (`aws s3 sync web/dist s3://fs/www/` after gzipping). User files are never touched. Mind LittleFS space during the update (old + new bundle) and that the IDE serves itself while it is replaced.
3. **Separate partition for the IDE**: put `www/` (and `fonts/`) in their own read-only image (e.g. the unused `img_data` partition, 1 MB, or a new one), flashed independently of the user `data` partition. Clean separation, but changes the partition table (serial flash once per device, like the other layout migration).

**Leaning:** 1 now (no firmware change, removes the trap), then 3 when the partition table is touched anyway; 2 only together with real auth. Also make the firmware recover from a missing registry index (rebuild it from the partition, or at least run `scripts/main.js`' bytecode if present).

## 7. TODO — Heap low point while the IDE loads

btn_a00002 reported `heapMinFree` 35.9 KB after a session of IDE use and S3 tests, against 70.3 KB right after boot (and 57.4 KB after the S3 test series alone). Suspect: the browser loading the ~315 KB gzipped bundle with several parallel connections (each with lwIP buffers and an AsyncWebServer request), plus the 2 s `/api/device` poll. Measure with the IDE open (serial heap probes or `/api/device` `heapMinFree` after each step), then consider: serving `www/` with fewer parallel connections (one bundle file, `Connection: keep-alive`), a cap on concurrent static responses, and replacing the poll by a push (see the device-info discussion).

## 8. TODO — Push device info instead of polling

The IDE polls `GET /api/device` every 2 s while the device-info face of the virtual device is shown: a new HTTP request (often a new TCP connection, then a TIME_WAIT pcb in lwIP) each time. SSE (`AsyncEventSource`) would add a second permanent connection with its own per-client queue. Instead push it over the WebSocket the IDE already holds open (`/ws/repl`): a `{"type":"device",...}` frame (~500 B, freed after sending) every ~5 s while a client is connected, with the static parts (topology, build identity) sent once on connect. Cleaner variant at the same cost: turn the socket into one device channel (`/ws`) carrying REPL, device info and later events as message types. Keep the HTTP endpoint for clients without the socket (slow fallback poll, only while the panel is visible).
