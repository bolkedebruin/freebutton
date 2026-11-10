#!/usr/bin/env python3
"""Build the device's TrueType fonts (media/fonts/*.ttf) from the sources here.

DEV-ONLY: the outputs are committed, so a normal build never runs this. Run it
when the sources, the coverage or the symbol mapping below change, then run
check_fonts.py and commit media/fonts/.

    medium.ttf    Montserrat Medium   subset, kerning only  (fnt_medium_*)
    semibold.ttf  Montserrat SemiBold subset, kerning only  (fnt_semibold_*)
    icons.ttf     Material Design Icons 7.4.47, every icon, plus media
                  control symbols (U+23E9-23FA) and check marks mapped to
                  MDI icons (fallback of both text fonts)

The device renders them with LVGL tiny_ttf (stb_truetype), which uses only
cmap, glyf/loca, hmtx/hhea and GPOS pair kerning: hinting, GSUB, GDEF, marks
and glyph names are dropped.

Offline and reproducible: no network access; the output depends only on the
committed sources and the fontTools version (pinned in requirements.txt).
Requires Python 3 with fontTools, e.g. in a throwaway venv:

    python3 -m venv /tmp/fbfonts
    /tmp/fbfonts/bin/pip install -r tools/fonts/requirements.txt
    /tmp/fbfonts/bin/python tools/fonts/build_fonts.py [--curated] [--out DIR]

--curated builds icons.ttf from the icon list in mdi_glyphs.txt only (~100 KB
instead of ~1.2 MB); the default ships every MDI icon so any icon picked on
pictogrammers.com/library/mdi works.
"""

import argparse
import logging
import os
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

MONTSERRAT_MEDIUM = os.path.join(HERE, "Montserrat-Medium.ttf")
MONTSERRAT_SEMIBOLD = os.path.join(HERE, "Montserrat-SemiBold.ttf")
MDI = os.path.join(HERE, "materialdesignicons-webfont.ttf")
MDI_CURATED = os.path.join(HERE, "mdi_glyphs.txt")

# Text coverage: every character Montserrat maps (968: Latin incl. Extended-A/B
# and Additional, Cyrillic, Greek Δ Ω μ π, punctuation, currency, arrows,
# math, a few shapes), ~77 KB per weight without hinting and GSUB. The
# minimum the UI needs (USER_SYMBOLS, LARGE_SYMBOLS, Latin ranges) is
# enforced by check_fonts.py.

# Characters that must not draw anything. Montserrat's U+200B (zero width
# space, advance 0) is mapped to them too; LVGL would otherwise draw a
# placeholder box. (tiny_ttf already skips U+200C itself.)
ZERO_WIDTH = [0x200D, 0x2060, 0xFE0E, 0xFE0F, 0xFEFF]

# Symbols Montserrat lacks -> MDI icon (by glyph name): the media controls
# of USER_SYMBOLS and check marks. Mapped in icons.ttf (no glyph is copied),
# they draw for both text fonts through the fallback.
MAPPED_SYMBOLS = {
    0x23E9: "fast-forward",   # ⏩
    0x23EA: "rewind",         # ⏪
    0x23ED: "skip-next",      # ⏭
    0x23EE: "skip-previous",  # ⏮
    0x23EF: "play-pause",     # ⏯
    0x23F4: "menu-left",      # ⏴ black medium left-pointing triangle
    0x23F5: "menu-right",     # ⏵
    0x23F6: "menu-up",        # ⏶
    0x23F7: "menu-down",      # ⏷
    0x23F8: "pause",          # ⏸
    0x23F9: "stop",           # ⏹
    0x23FA: "record",         # ⏺
    0x2713: "check",          # ✓
    0x2714: "check-bold",     # ✔
    0x2717: "close",          # ✗
    0x2718: "close-thick",    # ✘
}

# Name table: keep family/style/version and the copyright + license records.
NAME_IDS = [0, 1, 2, 3, 4, 5, 6, 13, 14]
MDI_DESCRIPTION = (
    "Subset for FreeButton: GSUB and glyph names removed; media symbols "
    "U+23E9-23FA and check marks U+2713-2718 mapped to MDI icons. "
    "Icons and font: Apache License 2.0, Pictogrammers "
    "(https://pictogrammers.com/library/mdi/)."
)
MDI_LICENSE_URL = "https://www.apache.org/licenses/LICENSE-2.0"


def text_codepoints(font):
    cps = set(font.getBestCmap())
    cps.discard(0)  # NULL, mapped by Montserrat; never drawn
    return cps


def curated_icons():
    with open(MDI_CURATED, encoding="ascii") as f:
        return {int(tok, 16) for tok in f.read().replace("\n", ",").split(",") if tok.strip()}


def subset_options(layout_features):
    o = subset.Options()
    o.layout_features = layout_features
    o.hinting = False            # stb_truetype does not hint
    o.glyph_names = False        # post format 3
    o.legacy_kern = False
    o.notdef_outline = False     # .notdef is never drawn (glyph 0 = not found)
    o.name_IDs = NAME_IDS
    o.name_languages = [0x0409]
    o.drop_tables += ["GSUB", "GDEF", "STAT", "gasp", "prep", "fpgm", "cvt ", "DSIG", "FFTM"]
    o.recalc_timestamp = False   # reproducible output
    o.recalc_bounds = True
    o.prune_unicode_ranges = True
    return o


def load(path):
    return TTFont(path, recalcTimestamp=False, lazy=False)


def subset_font(font, unicodes, layout_features):
    s = subset.Subsetter(subset_options(layout_features))
    s.populate(unicodes=sorted(unicodes))
    s.subset(font)


def set_cmap(font, mapping):
    """Rewrite cmap as (0,3)+(3,1) format 4 [+ (0,4)+(3,10) format 12].

    stb_truetype uses the LAST Unicode subtable it recognises in the record
    list (sorted by platform/encoding), so with plane-15 codepoints that must
    be the (3,10) format 12 table."""
    bmp = {cp: g for cp, g in mapping.items() if cp <= 0xFFFF}
    tables = []
    for plat, enc in ((0, 3), (3, 1)):
        t = CmapSubtable.newSubtable(4)
        t.platformID, t.platEncID, t.language = plat, enc, 0
        t.cmap = dict(bmp)
        tables.append(t)
    if any(cp > 0xFFFF for cp in mapping):
        for plat, enc in ((0, 4), (3, 10)):
            t = CmapSubtable.newSubtable(12)
            t.platformID, t.platEncID, t.language = plat, enc, 0
            t.cmap = dict(mapping)
            tables.append(t)
    font["cmap"].tables = tables


def build_text(src, out):
    font = load(src)
    subset_font(font, text_codepoints(font), ["kern"])
    mapping = dict(font.getBestCmap())
    zw = mapping.get(0x200B)
    if zw is None or font["hmtx"][zw][0] != 0:
        sys.exit("%s: no zero-advance U+200B glyph" % src)
    for cp in ZERO_WIDTH:
        mapping.setdefault(cp, zw)
    set_cmap(font, mapping)
    font.save(out)


def build_icons(out, curated):
    font = load(MDI)
    original = font.getBestCmap()
    by_name = {g: cp for cp, g in original.items()}
    missing = [n for n in MAPPED_SYMBOLS.values() if n not in by_name]
    if missing:
        sys.exit("MDI has no icon named %s" % ", ".join(missing))
    icons = curated_icons() if curated else set(original)
    icons.update(by_name[n] for n in MAPPED_SYMBOLS.values())
    unknown = sorted(cp for cp in icons if cp not in original)
    if unknown:
        sys.exit("mdi_glyphs.txt lists codepoints MDI lacks: %s" % ", ".join("%X" % c for c in unknown))
    subset_font(font, icons, [])
    mapping = dict(font.getBestCmap())
    for cp, name in MAPPED_SYMBOLS.items():
        mapping[cp] = name
    set_cmap(font, mapping)
    name = font["name"]
    name.setName(MDI_DESCRIPTION, 10, 3, 1, 0x409)
    name.setName(MDI_LICENSE_URL, 14, 3, 1, 0x409)
    name.setName("Copyright Pictogrammers (pictogrammers.com). Apache License 2.0.", 0, 3, 1, 0x409)
    font.save(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=os.path.join(REPO, "media", "fonts"), help="output directory")
    ap.add_argument("--curated", action="store_true", help="icons.ttf from mdi_glyphs.txt only")
    args = ap.parse_args()
    logging.getLogger("fontTools").setLevel(logging.ERROR)  # source-font warnings (post padding, timestamps)
    os.makedirs(args.out, exist_ok=True)
    outputs = [
        ("medium.ttf", lambda p: build_text(MONTSERRAT_MEDIUM, p)),
        ("semibold.ttf", lambda p: build_text(MONTSERRAT_SEMIBOLD, p)),
        ("icons.ttf", lambda p: build_icons(p, args.curated)),
    ]
    for name, build in outputs:
        path = os.path.join(args.out, name)
        build(path)
        f = TTFont(path)
        print("%-13s %8d bytes  %5d glyphs  %5d codepoints" % (
            name, os.path.getsize(path), len(f.getGlyphOrder()), len(f.getBestCmap())))


if __name__ == "__main__":
    main()
