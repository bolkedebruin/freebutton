# Default UI fonts

The device draws its UI with these TrueType files, rendered by LVGL
`tiny_ttf` from the media store (S3 bucket `fonts`, see
[docs/MEDIA-STORE.md](../../docs/MEDIA-STORE.md)). Upload a file under the same
key to replace a font; the device reloads its fonts 1.5 s after the last
upload or delete in the bucket (no reboot). A missing or invalid
file falls back to LVGL's built-in Montserrat 14 (text) or to no icons.

| Key | Used for | Source | License |
|---|---|---|---|
| `medium.ttf` | `fnt_medium_14/24/32/48/72` | Montserrat Medium 8.000 | SIL OFL 1.1 ([OFL-Montserrat.txt](OFL-Montserrat.txt)) |
| `semibold.ttf` | `fnt_semibold_14/18` | Montserrat SemiBold 8.000 | SIL OFL 1.1 ([OFL-Montserrat.txt](OFL-Montserrat.txt)) |
| `icons.ttf` | fallback of both text fonts | Material Design Icons webfont 7.4.47 | Apache 2.0 ([LICENSE-MaterialDesignIcons.txt](LICENSE-MaterialDesignIcons.txt), [LICENSE-Apache-2.0.txt](LICENSE-Apache-2.0.txt)) |

## Provenance

Built by [`tools/fonts/build_fonts.py`](../../tools/fonts/build_fonts.py)
(fontTools 4.68.0, pinned in `tools/fonts/requirements.txt`) from the sources
committed in `tools/fonts/`, offline and reproducibly; checked by
[`tools/fonts/check_fonts.py`](../../tools/fonts/check_fonts.py).

| File | SHA-256 |
|---|---|
| source `tools/fonts/Montserrat-Medium.ttf` | `20ebfd658f55a256d60b4f84849e0026e4b599926493738ed3952c7200218d33` |
| source `tools/fonts/Montserrat-SemiBold.ttf` | `1418bd4d74c35bfa473d404b8b94b3e83784efa32ed9875a28a2e83449d02942` |
| source `tools/fonts/materialdesignicons-webfont.ttf` | `61e8aba5a4e981fe22cf7c8e8bcdbea00476e75c62c37f01bf7ee33361d68428` (identical to `fonts/materialdesignicons-webfont.ttf` of [Templarian/MaterialDesign-Webfont v7.4.47](https://github.com/Templarian/MaterialDesign-Webfont/tree/v7.4.47)) |
| `medium.ttf` | `1bf642c570cc7f8b8cf4d2e964b4896ba86bae9fcb52b14e6cc194bddec5ea2b` |
| `semibold.ttf` | `4e6c538e5eca65b66236db8cc44f24b2784b23d2d08fe17ecf7898d4c97723c6` |
| `icons.ttf` | `15c44321cbd0d921e005ffb9abeabea29c511abd20aa247a05a83c3cb618f5e7` |

Montserrat: <https://github.com/JulietaUla/Montserrat>, Copyright 2011 The
Montserrat Project Authors. Material Design Icons: <https://pictogrammers.com/library/mdi/>.

## What the build changes

These are modified versions of the sources (both licenses allow it; OFL:
Montserrat has no Reserved Font Name).

- `medium.ttf`, `semibold.ttf`: every character Montserrat maps (968: Latin
  with Extended-A/B and Additional, Cyrillic, punctuation, currency, arrows,
  math, a few shapes). Hinting, `GSUB`, `GDEF`, `STAT`, mark positioning and
  glyph names removed: `tiny_ttf` does not use them. Kerning kept (`GPOS`
  `kern` feature only, which stb_truetype applies). U+200D, U+2060, U+FE0E,
  U+FE0F and U+FEFF are mapped to the zero-width space, so they draw nothing.
- `icons.ttf`: all 7447 MDI icons at their MDI codepoints (U+F0001-F1D17),
  `GSUB` and glyph names removed. Added mappings (to existing MDI icons, no
  glyph copied) for symbols Montserrat lacks: ⏩ U+23E9 fast-forward,
  ⏪ U+23EA rewind, ⏭ U+23ED skip-next, ⏮ U+23EE skip-previous,
  ⏯ U+23EF play-pause, ⏴ U+23F4 menu-left, ⏵ U+23F5 menu-right,
  ⏶ U+23F6 menu-up, ⏷ U+23F7 menu-down, ⏸ U+23F8 pause, ⏹ U+23F9 stop,
  ⏺ U+23FA record, ✓ U+2713 check, ✔ U+2714 check-bold, ✗ U+2717 close,
  ✘ U+2718 close-thick.

## Rebuild

```sh
python3 -m venv /tmp/fbfonts
/tmp/fbfonts/bin/pip install -r tools/fonts/requirements.txt
/tmp/fbfonts/bin/python tools/fonts/build_fonts.py      # writes media/fonts/*.ttf
/tmp/fbfonts/bin/python tools/fonts/check_fonts.py      # coverage + kerning check
```

`build_fonts.py --curated --out DIR` builds an `icons.ttf` with only the 681
icons of `tools/fonts/mdi_glyphs.txt` (~99 KB instead of ~1.06 MB; the 16 mapped symbols are always included).
