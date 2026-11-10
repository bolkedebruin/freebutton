#!/usr/bin/env python3
"""Check that media/fonts/*.ttf cover what the UI draws, the way the device
reads them (LVGL tiny_ttf = stb_truetype_htcw).

Checks, per file:
  * the tables stb_truetype needs exist (TrueType outlines, not CFF);
  * the cmap subtable stb_truetype picks (the LAST Unicode one in the record
    list) has a supported format and maps the required codepoints;
  * text fonts: Basic Latin, Latin-1, Latin Extended-A, USER_SYMBOLS and
    LARGE_SYMBOLS (minus the symbols icons.ttf maps), and the
    zero-width characters draw nothing;
  * text fonts: stb_truetype's GPOS kerning (ported below, byte for byte the
    same lookups) gives the same pair adjustments as the source font's 'kern'
    feature, for every pair of Latin characters;
  * icons.ttf: the mapped symbols (media controls, check marks), every icon in mdi_glyphs.txt, and every MDI
    codepoint (U+F0000-F1FFF) referenced in the repo: configs and scripts in
    data/ and devices/, the web IDE (web/src), firmware and tests;
  * non-ASCII text in committed configs and device scripts resolves through
    the fallback chain text -> icons (a warning: some of it is log output).

Exit status 1 on any failure. Requires fontTools (see build_fonts.py).

    /tmp/fbfonts/bin/python tools/fonts/check_fonts.py [--fonts DIR]
"""

import argparse
import logging
import os
import re
import struct
import sys

from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import build_fonts  # noqa: E402  (MAPPED_SYMBOLS, ZERO_WIDTH, sources)

# The symbols the UI promises (formerly lv_font_conv --symbols; U+FE0E is the
# text presentation selector, drawn as nothing).
USER_SYMBOLS = "°³€$£øØÅåæÆéèêàâïîôöùûüçäöüßÄÖÜë↑↓⏴︎⏵︎⏶︎⏷︎⏸︎⏹︎⏭︎⏮︎⏯︎₂±×÷≠≤≥√∞"
LARGE_SYMBOLS = "0123456789.,:-/APM°FC"
TEXT_RANGES = [(0x20, 0x7E), (0xA0, 0xFF), (0x100, 0x17F)]
KERN_RANGES = [(0x20, 0x7E), (0xA0, 0xFF), (0x100, 0x17F)]

ICON_FIRST, ICON_LAST = 0xF0000, 0xF1FFF

SCAN_DIRS = ["data", "devices", "web/src", "src", "test"]
SKIP_DIRS = {"node_modules", ".pio", ".git", "dist"}
DISPLAY_TEXT = re.compile(r"^(data/[^/]+\.json[^/]*|data/scripts/.*|devices/.*\.(json|js))$")
ESCAPE = re.compile(
    r"\\u\{([0-9a-fA-F]{1,6})\}"                               # JS \u{F0D43}
    r"|\\u([dD][89abAB][0-9a-fA-F]{2})\\u([dD][c-fC-F][0-9a-fA-F]{2})"  # surrogate pair
    r"|\\u([0-9a-fA-F]{4})"                                    # \uXXXX
    r"|\\U([0-9a-fA-F]{8})")                                   # \UXXXXXXXX
HEX_LITERAL = re.compile(r"\b0x(F[01][0-9A-F]{3})\b", re.IGNORECASE)  # fromCodePoint(0xF0D43)


# ---- stb_truetype, ported (stb_truetype_htcw.h in LVGL 9.6) ----------------

class Stb:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        self.tables = {}
        num = self.u16(4)
        for i in range(num):
            rec = 12 + 16 * i
            tag = self.data[rec:rec + 4].decode("latin-1")
            self.tables[tag] = self.u32(rec + 8)
        self.index_map = 0
        cmap = self.tables.get("cmap", 0)
        if cmap:
            for i in range(self.u16(cmap + 2)):  # stbtt_InitFont: the last one wins
                rec = cmap + 4 + 8 * i
                plat, enc = self.u16(rec), self.u16(rec + 2)
                if (plat == 3 and enc in (1, 10)) or plat == 0:
                    self.index_map = cmap + self.u32(rec + 4)
                    self.cmap_choice = (plat, enc)

    def u16(self, o):
        return struct.unpack_from(">H", self.data, o)[0]

    def s16(self, o):
        return struct.unpack_from(">h", self.data, o)[0]

    def u32(self, o):
        return struct.unpack_from(">I", self.data, o)[0]

    def cmap_format(self):
        return self.u16(self.index_map)

    def coverage_index(self, table, glyph):
        fmt = self.u16(table)
        if fmt == 1:
            l, r = 0, self.u16(table + 2) - 1
            while l <= r:
                m = (l + r) >> 1
                g = self.u16(table + 4 + 2 * m)
                if glyph < g:
                    r = m - 1
                elif glyph > g:
                    l = m + 1
                else:
                    return m
            return -1
        if fmt == 2:
            l, r = 0, self.u16(table + 2) - 1
            while l <= r:
                m = (l + r) >> 1
                rec = table + 4 + 6 * m
                start, end = self.u16(rec), self.u16(rec + 2)
                if glyph < start:
                    r = m - 1
                elif glyph > end:
                    l = m + 1
                else:
                    return self.u16(rec + 4) + glyph - start
            return -1
        return -1

    def glyph_class(self, table, glyph):
        fmt = self.u16(table)
        if fmt == 1:
            start, count = self.u16(table + 2), self.u16(table + 4)
            if start <= glyph < start + count:
                return self.u16(table + 6 + 2 * (glyph - start))
            return 0
        if fmt == 2:
            l, r = 0, self.u16(table + 2) - 1
            while l <= r:
                m = (l + r) >> 1
                rec = table + 4 + 6 * m
                start, end = self.u16(rec), self.u16(rec + 2)
                if glyph < start:
                    r = m - 1
                elif glyph > end:
                    l = m + 1
                else:
                    return self.u16(rec + 4)
            return 0
        return -1

    def kern_table_check(self):
        gpos = self.tables.get("GPOS")
        if gpos:
            if self.u16(gpos) != 1 or self.u16(gpos + 2) != 0:
                return False
            ll = gpos + self.u16(gpos + 8)
            for i in range(self.u16(ll)):
                if self.u16(ll + self.u16(ll + 2 + 2 * i)) == 2:
                    return True
            return False
        return "kern" in self.tables

    def kern(self, g1, g2):
        """stbtt__GetGlyphGPOSInfoAdvance (GPOS only; no 'kern' table here)."""
        gpos = self.tables.get("GPOS")
        if not gpos or self.u16(gpos) != 1 or self.u16(gpos + 2) != 0:
            return 0
        ll = gpos + self.u16(gpos + 8)
        for i in range(self.u16(ll)):
            lt = ll + self.u16(ll + 2 + 2 * i)
            if self.u16(lt) != 2:
                continue
            for sti in range(self.u16(lt + 4)):
                t = lt + self.u16(lt + 6 + 2 * sti)
                fmt = self.u16(t)
                ci = self.coverage_index(t + self.u16(t + 2), g1)
                if ci == -1:
                    continue
                vf1, vf2 = self.u16(t + 4), self.u16(t + 6)
                if fmt == 1:
                    if vf1 != 4 or vf2 != 0:
                        return 0
                    if ci >= self.u16(t + 8):
                        return 0
                    pvt = t + self.u16(t + 10 + 2 * ci)
                    l, r = 0, self.u16(pvt) - 1
                    while l <= r:
                        m = (l + r) >> 1
                        pv = pvt + 2 + 4 * m
                        g = self.u16(pv)
                        if g2 < g:
                            r = m - 1
                        elif g2 > g:
                            l = m + 1
                        else:
                            return self.s16(pv + 2)
                elif fmt == 2:
                    if vf1 != 4 or vf2 != 0:
                        return 0
                    c1 = self.glyph_class(t + self.u16(t + 8), g1)
                    c2 = self.glyph_class(t + self.u16(t + 10), g2)
                    n1, n2 = self.u16(t + 12), self.u16(t + 14)
                    if not (0 <= c1 < n1) or not (0 <= c2 < n2):
                        return 0
                    return self.s16(t + 16 + 2 * (c1 * n2) + 2 * c2)
                else:
                    return 0
        return 0


# ---- reference kerning from the source font (fontTools objects) ------------

def reference_kern(font):
    """(glyph1, glyph2) -> x advance adjustment of the 'kern' feature, with the
    OpenType rule: per lookup, the first subtable that covers the pair."""
    gpos = font["GPOS"].table
    lookups = sorted({i for fr in gpos.FeatureList.FeatureRecord if fr.FeatureTag == "kern"
                      for i in fr.Feature.LookupListIndex})
    subtables = []
    for i in lookups:
        lk = gpos.LookupList.Lookup[i]
        sts = lk.SubTable
        if lk.LookupType == 9:
            sts = [st.ExtSubTable for st in sts]
        subtables.append([st for st in sts if getattr(st, "LookupType", 2) == 2 or lk.LookupType == 2])

    def adjust(g1, g2):
        total = 0
        for sts in subtables:
            for st in sts:
                cov = st.Coverage.glyphs
                if g1 not in cov:
                    continue
                if st.Format == 1:
                    ps = st.PairSet[cov.index(g1)]
                    rec = next((r for r in ps.PairValueRecord if r.SecondGlyph == g2), None)
                    if rec is None:
                        continue
                    total += getattr(rec.Value1, "XAdvance", 0) if rec.Value1 else 0
                    break
                c1 = st.ClassDef1.classDefs.get(g1, 0)
                c2 = st.ClassDef2.classDefs.get(g2, 0)
                v = st.Class1Record[c1].Class2Record[c2].Value1
                total += getattr(v, "XAdvance", 0) if v else 0
                break
        return total
    return adjust


# ---- repo scan --------------------------------------------------------------

def scan_repo():
    """codepoint -> set of files, for non-ASCII characters and escapes."""
    found = {}
    for top in SCAN_DIRS:
        for dp, dns, fns in os.walk(os.path.join(REPO, top)):
            dns[:] = [d for d in dns if d not in SKIP_DIRS]
            for fn in fns:
                path = os.path.join(dp, fn)
                rel = os.path.relpath(path, REPO)
                try:
                    if os.path.getsize(path) > 4_000_000:
                        continue
                    text = open(path, encoding="utf-8").read()
                except (UnicodeDecodeError, OSError):
                    continue
                cps = {ord(c) for c in text if ord(c) > 0x7F}
                for m in ESCAPE.finditer(text):
                    if m.group(1):
                        cps.add(int(m.group(1), 16))
                    elif m.group(2):
                        hi, lo = int(m.group(2), 16), int(m.group(3), 16)
                        cps.add(0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00))
                    elif m.group(4):
                        cps.add(int(m.group(4), 16))
                    else:
                        cps.add(int(m.group(5), 16))
                if fn.endswith((".js", ".ts")):
                    cps.update(int(h, 16) for h in HEX_LITERAL.findall(text))
                for cp in cps:
                    found.setdefault(cp, set()).add(rel)
    return found


# ---- checks -----------------------------------------------------------------

class Report:
    def __init__(self):
        self.failed = False

    def ok(self, msg):
        print("  ok    " + msg)

    def fail(self, msg):
        self.failed = True
        print("  FAIL  " + msg)

    def warn(self, msg):
        print("  warn  " + msg)


def fmt_cps(cps, limit=24):
    cps = sorted(cps)
    s = " ".join("U+%04X" % c for c in cps[:limit])
    return s + (" ... (+%d)" % (len(cps) - limit) if len(cps) > limit else "")


def stb_cmap(rep, name, stb, font):
    for tag in ("cmap", "loca", "head", "glyf", "hhea", "hmtx"):
        if tag not in stb.tables:
            rep.fail("%s: no '%s' table (stb_truetype needs TrueType outlines)" % (name, tag))
            return {}
    if not stb.index_map:
        rep.fail("%s: no Unicode cmap subtable stb_truetype understands" % name)
        return {}
    fmt = stb.cmap_format()
    if fmt not in (0, 4, 6, 12, 13):
        rep.fail("%s: stb_truetype picks cmap (%d,%d) format %d, unsupported" % ((name,) + stb.cmap_choice + (fmt,)))
        return {}
    plat, enc = stb.cmap_choice
    table = next(t for t in font["cmap"].tables if (t.platformID, t.platEncID) == (plat, enc))
    rep.ok("%s: stb_truetype uses cmap (%d,%d) format %d, %d codepoints" % (name, plat, enc, fmt, len(table.cmap)))
    return table.cmap


def require(rep, name, cmap, cps, what):
    missing = {c for c in cps if c not in cmap}
    if missing:
        rep.fail("%s lacks %d %s: %s" % (name, len(missing), what, fmt_cps(missing)))
    else:
        rep.ok("%s has all %d %s" % (name, len(cps), what))


def check_kerning(rep, name, path, source):
    stb = Stb(path)
    if not stb.kern_table_check():
        rep.fail("%s: stbtt_KernTableCheck() = 0, tiny_ttf would disable kerning" % name)
        return
    out = TTFont(path)
    src = TTFont(source)
    ref = reference_kern(src)
    out_cmap, src_cmap = out.getBestCmap(), src.getBestCmap()
    cps = [c for a, b in KERN_RANGES for c in range(a, b + 1) if c in out_cmap and c in src_cmap]
    gid = {c: out.getGlyphID(out_cmap[c]) for c in cps}
    pairs = nonzero = mismatches = 0
    first_bad = None
    for c1 in cps:
        for c2 in cps:
            want = ref(src_cmap[c1], src_cmap[c2])
            got = stb.kern(gid[c1], gid[c2])
            pairs += 1
            nonzero += want != 0
            if got != want:
                mismatches += 1
                first_bad = first_bad or (c1, c2, want, got)
    if mismatches:
        c1, c2, want, got = first_bad
        rep.fail("%s: stb kerning differs from the source in %d of %d pairs (first %r%r: %d vs %d)" % (
            name, mismatches, pairs, chr(c1), chr(c2), want, got))
    else:
        rep.ok("%s: stb_truetype kerning = source 'kern' for %d pairs (%d kerned)" % (name, pairs, nonzero))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fonts", default=os.path.join(REPO, "media", "fonts"))
    ap.add_argument("--no-kern", action="store_true", help="skip the (slow-ish) kerning comparison")
    args = ap.parse_args()
    logging.getLogger("fontTools").setLevel(logging.ERROR)
    rep = Report()

    media = set(build_fonts.MAPPED_SYMBOLS)
    zero_width = set(build_fonts.ZERO_WIDTH) | {0x200B}
    text_need = {c for a, b in TEXT_RANGES for c in range(a, b + 1)}
    text_need |= {ord(c) for c in USER_SYMBOLS + LARGE_SYMBOLS}
    text_need -= media | zero_width

    found = scan_repo()
    repo_icons = {c for c in found if ICON_FIRST <= c <= ICON_LAST}
    curated = build_fonts.curated_icons()

    cmaps = {}
    fonts = {}
    for name, source in (("medium.ttf", build_fonts.MONTSERRAT_MEDIUM),
                         ("semibold.ttf", build_fonts.MONTSERRAT_SEMIBOLD),
                         ("icons.ttf", build_fonts.MDI)):
        path = os.path.join(args.fonts, name)
        print("%s (%d bytes)" % (name, os.path.getsize(path)))
        font = TTFont(path)
        fonts[name] = font
        cmap = stb_cmap(rep, name, Stb(path), font)
        cmaps[name] = cmap
        if name == "icons.ttf":
            require(rep, name, cmap, media, "mapped symbols (media controls, check marks)")
            require(rep, name, cmap, curated, "icons of mdi_glyphs.txt")
            require(rep, name, cmap, repo_icons, "MDI icons referenced in the repo")
            for c in sorted(repo_icons):
                if c not in cmap:
                    rep.fail("  U+%05X used in %s" % (c, ", ".join(sorted(found[c])[:4])))
        else:
            require(rep, name, cmap, text_need, "text codepoints (Latin, USER_SYMBOLS, LARGE_SYMBOLS)")
            hmtx = font["hmtx"]
            wide = [c for c in zero_width if c not in cmap or hmtx[cmap[c]][0] != 0]
            if wide:
                rep.fail("%s: zero-width characters missing or with an advance: %s" % (name, fmt_cps(wide)))
            else:
                rep.ok("%s: %s draw nothing" % (name, fmt_cps(zero_width)))
            shadow = media & set(cmap)
            if shadow:
                rep.warn("%s maps mapped symbols itself (icons.ttf's are not used): %s" % (name, fmt_cps(shadow)))
            if not args.no_kern:
                check_kerning(rep, name, path, source)

    # What the screen may show: text from configs and device scripts.
    print("display text in configs and device scripts")
    for text_font in ("medium.ttf", "semibold.ttf"):
        missing = {}
        for c, files in found.items():
            if not any(DISPLAY_TEXT.match(f) for f in files) or c in zero_width:
                continue
            if c in cmaps[text_font] or c in cmaps["icons.ttf"]:
                continue
            missing[c] = sorted(f for f in files if DISPLAY_TEXT.match(f))
        if missing:
            rep.warn("%s -> icons.ttf does not draw %d codepoints (placeholder box if shown):" % (text_font, len(missing)))
            for c in sorted(missing):
                rep.warn("  U+%04X %s  in %s" % (c, chr(c), ", ".join(missing[c][:3])))
        else:
            rep.ok("%s -> icons.ttf draws every non-ASCII character there" % text_font)

    print("repo: %d distinct MDI icons referenced: %s" % (len(repo_icons), fmt_cps(repo_icons)))
    if rep.failed:
        print("FAILED")
        return 1
    print("all font checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
