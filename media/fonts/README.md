# Default UI fonts

The device draws its UI with these TrueType files, rendered by LVGL
`tiny_ttf` from the media store (S3 bucket `fonts`, see
[docs/MEDIA-STORE.md](../../docs/MEDIA-STORE.md)). `medium.ttf` and
`semibold.ttf` are the two system fonts of the font registry
(`src/ui/fonts/fonts.c`): they always exist, and an S3 DELETE of them is
refused. The firmware's own screens (modals, progress bar) do not use them:
they use the Montserrat 12 that is compiled into the firmware. `icons.ttf` is an ordinary font file
that config.json names in its top-level `"fonts"` (the factory config calls it
`mdi` and makes it the fallback of the system fonts; see
[docs/STYLE.md](../../docs/STYLE.md)). Upload a file under the same key to
replace a font; the device reloads its fonts 1.5 s after the last upload or
delete in the bucket (no reboot). A missing or invalid file falls back to
LVGL's built-in Montserrat 12.

| Key | Font | Source | License |
|---|---|---|---|
| `medium.ttf` | system font `montserrat` (UI text; protected) | Montserrat Medium 8.000 | SIL OFL 1.1 ([OFL-Montserrat.txt](OFL-Montserrat.txt)) |
| `semibold.ttf` | system font `montserrat_semibold` (captions, button top labels; protected) | Montserrat SemiBold 8.000 | SIL OFL 1.1 ([OFL-Montserrat.txt](OFL-Montserrat.txt)) |
| `icons.ttf` | none by itself; `mdi` in the factory config (Material Design Icons) | Material Design Icons webfont 7.4.47 | Apache 2.0 ([LICENSE-MaterialDesignIcons.txt](LICENSE-MaterialDesignIcons.txt), [LICENSE-Apache-2.0.txt](LICENSE-Apache-2.0.txt)) |

## Provenance

The files are the upstream fonts, unmodified: byte-identical copies of the
sources in `tools/fonts/` (the native test `test_ttf_check` checks this).
Nothing is built.

| File | Copy of | SHA-256 |
|---|---|---|
| `medium.ttf` (198,104 B) | `tools/fonts/Montserrat-Medium.ttf` | `20ebfd658f55a256d60b4f84849e0026e4b599926493738ed3952c7200218d33` |
| `semibold.ttf` (198,204 B) | `tools/fonts/Montserrat-SemiBold.ttf` | `1418bd4d74c35bfa473d404b8b94b3e83784efa32ed9875a28a2e83449d02942` |
| `icons.ttf` (1,307,660 B) | `tools/fonts/materialdesignicons-webfont.ttf`, identical to `fonts/materialdesignicons-webfont.ttf` of [Templarian/MaterialDesign-Webfont v7.4.47](https://github.com/Templarian/MaterialDesign-Webfont/tree/v7.4.47) | `61e8aba5a4e981fe22cf7c8e8bcdbea00476e75c62c37f01bf7ee33361d68428` |

Montserrat: <https://github.com/JulietaUla/Montserrat>, Copyright 2011 The
Montserrat Project Authors. Material Design Icons: <https://pictogrammers.com/library/mdi/>.

## What the device uses

- Montserrat: its Unicode cmaps are BMP only (format 4), which covers its
  Latin, Cyrillic and symbol characters. Kerning comes from `GPOS` pair
  adjustment, which stb_truetype applies. Hinting, `GSUB` and the other
  tables are ignored.
- `icons.ttf`: all 7447 MDI icons at their MDI codepoints (U+F0001-F1D17).
  stb_truetype reads the last Unicode cmap record of a font, which in the
  MDI webfont is (3,10) format 12, so the plane-15 codepoints work. Write
  them as `\u{F0599}` in scripts and display values.
- Symbols Montserrat lacks (⏩ ⏭ ✓ ✗ and similar) are not in MDI's cmap
  either: use the MDI icon, for example `\u{F012C}` (check) or `\u{F0156}`
  (close).
- `ttf_check` (`src/ui/fonts/ttf_check.c`) validates a file before
  stb_truetype reads it. Any other TrueType (`glyf`) font that passes it
  can be uploaded and used; CFF-outline OTF files are rejected.
