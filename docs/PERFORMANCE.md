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

**ESP32-S3 SIMD (PIE).**
- GCC does not generate PIE instructions; they need hand-written assembly.
- Espressif's esp_lvgl_port assembly targets LVGL 9.1 and only unmasked RGB565
  fill and copy.
- Our hot paths are masked blends to RGB565_SWAPPED.

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
