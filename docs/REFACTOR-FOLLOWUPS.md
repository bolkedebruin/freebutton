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

## 4. Known CI caveat — `crt.S` clean-build race (bug-004)

Full-clean firmware builds **intermittently** fail with `Source .../rmaker_*.crt.S not found`. Root cause: `esp_insights`/`esp_rainmaker` (transitive deps of the Arduino-ESP32 framework, disabled in `sdkconfig.defaults` but still embedding certs) — SCons races the IDF ninja generate step. It is NOT removable via our config (framework-level). CI mitigates with a single retry (`pio run -e esp32 || pio run -e esp32`); a double-failure would still go red. A proper fix needs upstream PlatformIO/pioarduino attention or a component-exclusion hack. Prefer incremental builds locally; only `rm -rf .pio/build/esp32` when you hit the separate `sdkconfig` double-output collision (a `-DSDKCONFIG=${PROJECT_BUILD_DIR}/sdkconfig` artifact — also a cleanup candidate).

---

## 5. Commit plan (branch `rework/build-release-sync`)

Refactor committed in workstream chunks; **pre-existing uncommitted work is left untouched** (HardwareInitializer, ButtonManager, DisplayManager, PushButton, driver changes, `CLAUDE.md` hardware edits) and so is the `.wolf/`/`.claude/` infra — those are yours to handle.
1. mquickjs submodule + engine/firmware split (`.gitmodules`, `lib/mquickjs`, `src/scripting/js/*`, `tools/js/*` minus wasm, generate scripts, `mquickjs_stdlib.c`)
2. fonts tracked + offline generation (`src/ui/fonts/mnt_*.c`, `generatefonts.sh`, `tools/fonts/mdi_glyphs.txt`, `.gitignore`)
3. version SoT + reproducible deps + build cleanup (`VERSION`, `sync_version.py`, `build_info_flags.py`, `platformio.ini` deps, `sdkconfig.defaults` TLS fix, dead-file deletions)
4. device serves web IDE + wasm (`WebServer.cpp`, `config.h` CDN removal, partition csv, `web/*`, real `web/public/assets/mquickjs.*`, `build_wasm.sh`, `assemble_littlefs.py`, `data/fonts/*`, `data/config.json`, `data/virt.json`) + the Lit upload wiring + validator feature
5. unified CI/release (`.github/workflows/*`, retire `update_version.py`, `tools/README.md`)
