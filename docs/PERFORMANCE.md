# Rendering performance

How long a full repaint of each display takes, what was tried to make it
faster, and what is kept. Measured on .81 (btn_a00001: main display with the
clock, weather and sensor items; six key displays) with
`tools/scripts/bench.py`.

## Measuring

`POST /api/bench?n=N` (signed) makes the LVGL task repaint every display N
times. `GET /api/bench` returns, per display:
- the average and best repaint time, to the last band on the wire;
- the time spent waiting for DMA;
- the bands flushed;
- the glyphs rasterized and the glyph lookups (metrics and kerning), with
  their CPU time (`fonts_stats()`).

`tools/scripts/bench.py http://<ip> -n 5 --json run.json --compare before.json`
prints the table.

## Results (2026-11-12, average of 5 repaints)

| Step | main | key displays | Kept |
|---|---|---|---|
| Baseline: flash DIO 80 MHz, LVGL at -Os, one 19.2 KB main buffer, one 2.4 KB mini buffer | 183-189 ms | 72-103 ms | |
| `-O2` for LVGL's renderer, fonts, LovyanGFX and src/ui | ±0 | ±0 | no: +76 KB of flash, no gain |
| 32 KB instruction cache | - | - | no: the app does not boot with it, even with a matching bootloader |
| **Flash QIO** (bootloader, USB flash once) | **135 ms (-29%)** | **50-71 ms (-30..-40%)** | yes |
| `-Os` instead of `-O2` with QIO | ±0 | ±0 | `-Os` (smaller) |
| Single-precision math in stb_truetype | ±0 | ±0 | yes: no software double routines, pixel-identical |
| B: double buffers of half size (main 2 x 9.6 KB, minis 2 x 1.2 KB) | 174 ms (+30%) | 102-143 ms (+100%) | no |
| C: double buffers of full size (main 2 x 19.2 KB, minis 2 x 2.4 KB) | 104 ms (-22%) | 46-66 ms (-5%) | no: free heap 38 KB, low point 29 KB |
| **D: one 9.6 KB mini buffer** (3 bands instead of 12) | 131 ms | **22-28 ms (-55%)** | yes, +7.2 KB of heap (shared by all minis) |

Final state on .81, with QIO, `-Os`, float math and D:
- main 131 ms, of which 30 ms waiting for DMA, 12 ms rasterizing and 2 ms in
  glyph lookups;
- key displays 22-28 ms;
- free heap 51.8 KB, low point 47 KB.

## What decides the speed

- **Code and data come from flash.**
  - The renderer runs from a 16 KB instruction cache, and the TTF outlines
    and image rasters are read from memory-mapped flash.
  - QIO doubles the flash bandwidth, and that was the biggest single gain.
  - `-O2` adds code and does not help.
- **The number of bands matters more than the pixels.**
  - Per band, LVGL walks the object tree, looks up every glyph of every
    label, and rasterizes again every glyph that crosses the band.
  - Halving the buffer doubled the bands and the time.
  - Making the shared mini buffer four times taller halved the minis'
    time.
- **Rasterizing is 10-25% of a repaint.** A glyph cache helps less than the
  band count does.

## Not done

**ESP32-S3 SIMD (PIE).** Measured, not worth it.

- `src/ui/render_stats.c` wraps LVGL's `lv_draw_sw_blend` and
  `lv_draw_sw_mask_apply` at link time and reports their time per pixel
  kind in `/api/bench`. Results on .81 (QIO, D):

  | Display | Opaque fills | Colour through a mask (glyphs, A8 icons, AA edges) | Mask generation (clock lines, arcs) | Of a repaint |
  |---|---|---|---|---|
  | main | 1.0 ms | 7.9 ms | 5.2 ms | ~14 of 131 ms (11%) |
  | key displays | 0.2 ms | 0.5-0.9 ms | – | ~0.7 of 22-28 ms (3%) |

- **Upper bound.** Even a 5x faster SIMD blend would save ~8 ms on main and
  under 1 ms on a key display.
- **Opaque fills** already run at 0.4 cycles/pixel. The masked blends cost
  13-50 cycles/pixel because they cover small areas (per-call overhead),
  which SIMD does not remove.
- **The rest of a repaint** is LVGL's control code: object tree, draw
  tasks, clipping. The main display spends ~72 ms there; the key displays
  ~12 ms. Glyph rasterizing (float math) and lookups make up 15-25%.
- **Why the instructions do not reach the hot paths:**
  - PIE has integer vectors only, so stb_truetype's float rasterizer cannot
    use it.
  - GCC does not generate PIE instructions; they need hand-written assembly.
  - Espressif's esp_lvgl_port assembly targets LVGL 9.1 and covers only
    unmasked RGB565 fill and copy. Our hot paths are masked blends to
    RGB565_SWAPPED.

**PPA and 2D-DMA.** These are ESP32-P4 only.

**A taller main buffer (fewer bands) or double buffering on main.** Each costs
about 19 KB of heap, which a board without PSRAM cannot spare today.

## QIO on a device

- The second-stage bootloader enables QIO (`CONFIG_ESPTOOLPY_FLASHMODE_QIO`).
- The image headers keep saying DIO, by design, so the ROM can always boot.
- An OTA does not replace the bootloader. A device needs one USB flash
  (`pio run -e esp32 -t upload`) to get QIO, and afterwards OTA as usual.
- Flash chip of .81: GigaDevice GD25Q128, quad enabled in eFuse.
- Still to verify before QIO is the repo default: a QIO-built app delivered
  by OTA to a device that still has the DIO bootloader (bench device).
