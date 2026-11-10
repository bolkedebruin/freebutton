# Build/Release Refactor — Follow-ups & Handoff

Companion to the completed effort on branch `rework/build-release-sync`.
Full plan: `~/.claude/plans/abundant-nibbling-sundae.md`. Learnings: `.wolf/cerebrum.md`, bugs: `.wolf/buglog.json`.

---

## 1. DONE — Streaming fonts (WS-G)

The UI fonts are the `.bin` files on LittleFS (`data/fonts/*.bin` → `/fonts/`), read on demand by `src/ui/fonts/fonts.c` + `font_bin.c` through LVGL's public font API only (no LVGL internals, so no pin on the LVGL version for this). The 7 compiled `mnt_*.c` fonts are gone from the firmware (app ~3.14 MB → ~2.16 MB, flash 85.6% → 58.9%); they live on in `test/font_reference/` as the reference for `test/test_font_bin`, which checks every glyph, kerning pair and code point of the `.bin` files against them.

Fallback: LVGL's built-in Montserrat 14 (`LV_FONT_DEFAULT`, ASCII + LVGL symbols, already compiled in). A font whose file is missing or invalid is replaced by it, and each streamed font falls back to it per glyph, so the device still shows text (at 14 px, without icons).

Measured on btn_a00002: ~490 B LVGL pool per font; the first 100 glyphs cost 170 file reads / 83 ms (shared 1 KB block cache, at most two open font files, kerning classes cached per glyph).

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
1. **Save + restore around `uploadfs`** (cheapest): a PlatformIO pre/post hook or `tools/` script that, before `uploadfs`, downloads every user object through the S3 API (`GET /app?list-type=2`; bucket `app` no longer contains `www/`, `fonts/` or `scripts/examples/`, see section 10) and the bytecode names (`/api/bytecode/list`), and afterwards puts them back (S3 PUT, and the bytecode via `/api/bytecode/upload` or a fresh compile of `scripts/main.js`). Works only while the device is on the network; serial-only flashes still lose data.
2. **Update `www/` through the S3 API**: allow writes to `www/` (and `fonts/`) for an authenticated client (needs the SigV4 / shared-key auth that is not there yet), and ship IDE updates as object uploads (`aws s3 sync web/dist s3://web/` after gzipping; bucket `web`, see section 10). User files are never touched. Mind LittleFS space during the update (old + new bundle) and that the IDE serves itself while it is replaced.
3. **Separate partition for the IDE**: put `www/` (and `fonts/`) in their own read-only image (e.g. the unused `img_data` partition, 1 MB, or a new one), flashed independently of the user `data` partition. Clean separation, but changes the partition table (serial flash once per device, like the other layout migration).

**Leaning:** 1 now (no firmware change, removes the trap), then 3 when the partition table is touched anyway; 2 only together with real auth. Also make the firmware recover from a missing registry index (rebuild it from the partition, or at least run `scripts/main.js`' bytecode if present).

## 7. TODO — Heap low point while the IDE loads

btn_a00002 reported `heapMinFree` 35.9 KB after a session of IDE use and S3 tests, against 70.3 KB right after boot (and 57.4 KB after the S3 test series alone). Suspect: the browser loading the ~315 KB gzipped bundle with several parallel connections (each with lwIP buffers and an AsyncWebServer request), plus the 2 s `/api/device` poll. Measure with the IDE open (serial heap probes or `/api/device` `heapMinFree` after each step), then consider: serving `www/` with fewer parallel connections (one bundle file, `Connection: keep-alive`), a cap on concurrent static responses, and replacing the poll by a push (see the device-info discussion).

## 8. TODO — Push device info instead of polling

The IDE polls `GET /api/device` every 2 s while the device-info face of the virtual device is shown: a new HTTP request (often a new TCP connection, then a TIME_WAIT pcb in lwIP) each time. SSE (`AsyncEventSource`) would add a second permanent connection with its own per-client queue. Instead push it over the WebSocket the IDE already holds open (`/ws/repl`): a `{"type":"device",...}` frame (~500 B, freed after sending) every ~5 s while a client is connected, with the static parts (topology, build identity) sent once on connect. Cleaner variant at the same cost: turn the socket into one device channel (`/ws`) carrying REPL, device info and later events as message types. Keep the HTTP endpoint for clients without the socket (slow fallback poll, only while the panel is visible).

## 9. DONE — Re-relocate bytecode after a firmware update

Since `dee8c6f` the registry refuses bytecode uploaded for another app build: relocation writes absolute addresses of the stdlib's ROM atoms, which live in the app's rodata. Now the device fixes that itself at boot, without the position-independent original:

- Each registry entry also records where the stdlib ROM table was (`rom`) and which table it was (`lib`, `bytecode_stdlib_id()`: a CRC32 of `js_stdlib_table` with its references to itself counted as offsets, so it is the same in every build with the same table, wherever the table lies).
- At boot, before any bytecode is found (`BytecodeRegistry::rebaseStale()`, called from `main.cpp`): an entry for another app build with the same `lib` is streamed from mapped flash through a 4 KB buffer into a new region by `JS_RebaseBytecodeWindow()` (engine fork). The bytecode's own pointers move to the new region, and the pointers to ROM atoms move by the table's displacement (same table, so every atom keeps its offset). No JS context, no atom lookups. A pointer into neither range fails the rebase instead of being guessed. The result is read back and CRC-checked, then the entry is replaced and the old region is free.
- Entries with another `lib` (the new firmware changed the stdlib: new or changed built-ins), or from before these fields existed, stay stale and are logged: "deploy it again". The IDE's wasm compiler has to be rebuilt in that case anyway.
- Verified on btn_a00001: a build whose table moved by +128 bytes, then the next by −128, both booted the script without a deploy (rebase ~130 ms at boot).
- Still open: the IDE could notice `"stale": true` in `/api/bytecode/list` and offer "Redeploy main.js" for the stdlib-changed case.

## 10. TODO — Separate partitions for user data and the IDE

Today one LittleFS `data` partition (4 MB) holds the IDE bundle (`www/`, ~1.5 MB gzipped), the fonts and the user's files, so every IDE update has to rewrite (and preserve) user data; `tools/scripts/ota_fs.py` does that by backing the user files up over S3 and rebuilding the image around them. Split it:

- **`ide` partition** (read-only for the firmware): `www/` and `fonts/`, built and updated as a whole image (`pio run -t buildfs` / filesystem OTA to that partition only). Size it for the bundle plus headroom (~2 MB), mounted e.g. at `/ide`, served by `serveStatic`; S3 lists it read-only (or not at all).
- **`data` partition** (user): `config.json`, `scripts/`, the bytecode registry index, user assets. Never rewritten by updates; formatted only on first boot / factory reset.
- The unused `img_data` (1 MB, subtype 0x42) can be folded into one of them.
- Needs a partition-table change, so one serial flash per device (as with the earlier layout migration; Garage still has the old table and needs that anyway). Plan the migration: back up user files over S3, flash the new table + both images, restore. OTA for the app and the IDE image separately afterwards; `ota_fs.py` then becomes a plain IDE-image upload.

**One S3 bucket per kind of content — DONE (single partition).** The S3 API no longer has the one bucket `fs`; it has one bucket per kind of content, each a directory on today's `data` partition (table `s3::kBuckets` in `src/core/S3Logic.h`):

| bucket | directory | access |
|---|---|---|
| `app` | `/` minus the other buckets' directories | read/write (.js/.json/.txt, 64 KiB, `config.json` validated and not deletable, `js_bytecode_meta.json` protected) |
| `web` | `/www` (IDE bundle, gzipped) | read-only (PUT/DELETE: 403 "bucket is read-only") |
| `examples` | `/scripts/examples` | read-only |
| `fonts` | media store (`media` partition, docs/MEDIA-STORE.md), not LittleFS | read/write (signed; flat `.ttf`/`.otf` keys, <= 2 MiB); GET/HEAD reach it only from S3 clients, because `/fonts/*.woff2` is also the IDE's own font URL |

Keys are relative to the bucket directory; an object is reachable through exactly one bucket (`app` does not list `www/`, `scripts/examples/`, answers 404 for reads and 400 InvalidArgument for writes of such keys). The IDE edits `app` and lists the read-only buckets on demand; `ota_fs.py` backs up all of `app`. Moving a bucket to its own partition later only changes its directory (mount point) in the table, not the API.

**Authentication: DONE** (SigV4, docs/AUTH.md): writes and reads of `app` are signed; `web`, `examples`, `fonts` are publicly readable.

**Still TODO:** the partition split above, and letting signed clients write `web` (`fonts` is writable since it moved to the media store), so it becomes writable for IDE updates as `aws s3 sync web/dist s3://web/` (gzipped objects) without touching user data or rebuilding images. A writable bucket below the root also needs `PutState::path` / `kWriteKeyMax` to account for its directory (static_assert in `S3Server.cpp`).
