// Sanity check of a TrueType file before stb_truetype parses it. See
// ttf_check.h. The structures and the order they are read in follow
// stb_truetype_htcw.h (LVGL 9.6): stbtt_InitFont, stbtt_FindGlyphIndex,
// stbtt_GetGlyphHMetrics, stbtt__GetGlyfOffset, stbtt_KernTableCheck and the
// GPOS / kern pair lookups.

#include "ttf_check.h"

#include <string.h>

typedef struct {
    const uint8_t *d;
    size_t size;
} buf_t;

static uint16_t u16(const buf_t *b, size_t o) { return (uint16_t)((b->d[o] << 8) | b->d[o + 1]); }
static int16_t s16(const buf_t *b, size_t o) { return (int16_t)u16(b, o); }
static uint32_t u32(const buf_t *b, size_t o) {
    return ((uint32_t)b->d[o] << 24) | ((uint32_t)b->d[o + 1] << 16) | ((uint32_t)b->d[o + 2] << 8) | b->d[o + 3];
}

/* [off, off + len) lies inside the file (no overflow) */
static bool in(const buf_t *b, size_t off, size_t len) { return off <= b->size && len <= b->size - off; }

typedef struct {
    size_t off;
    size_t len;
} table_t;

static bool find_table(const buf_t *b, uint16_t count, const char *tag, table_t *out) {
    for (uint16_t i = 0; i < count; i++) {
        size_t rec = 12 + 16u * i;
        if (memcmp(b->d + rec, tag, 4) == 0) {
            out->off = u32(b, rec + 8);
            out->len = u32(b, rec + 12);
            return true;
        }
    }
    return false;
}

/* Coverage table as stbtt__GetCoverageIndex reads it; returns the number of
 * covered glyphs (the largest index + 1 it can return), -1 if broken. */
static int32_t coverage_count(const buf_t *b, size_t t) {
    if (!in(b, t, 4)) return -1;
    uint16_t fmt = u16(b, t), n = u16(b, t + 2);
    if (fmt == 1) return in(b, t + 4, 2u * n) ? n : -1;
    if (fmt == 2) {
        if (!in(b, t + 4, 6u * n)) return -1;
        int32_t max = 0;
        for (uint16_t i = 0; i < n; i++) {
            size_t r = t + 4 + 6u * i;
            uint16_t start = u16(b, r), end = u16(b, r + 2), first = u16(b, r + 4);
            if (end < start) return -1;
            int32_t last = (int32_t)first + (end - start) + 1;
            if (last > max) max = last;
        }
        return max;
    }
    return 0; /* other formats: stb returns -1 (not covered) without reading */
}

static bool class_def_ok(const buf_t *b, size_t t) {
    if (!in(b, t, 4)) return false;
    uint16_t fmt = u16(b, t);
    if (fmt == 1) return in(b, t, 6) && in(b, t + 6, 2u * u16(b, t + 4));
    if (fmt == 2) return in(b, t + 4, 6u * u16(b, t + 2));
    return true; /* stb returns -1 without reading further */
}

static ttf_check_t check_gpos(const buf_t *b, table_t gpos) {
    const size_t g = gpos.off;
    if (!in(b, g, 10)) return TTF_ERR_GPOS;
    if (u16(b, g) != 1 || u16(b, g + 2) != 0) return TTF_OK; /* stb ignores other versions */
    size_t ll = g + u16(b, g + 8);
    if (!in(b, ll, 2)) return TTF_ERR_GPOS;
    uint16_t lookups = u16(b, ll);
    if (!in(b, ll + 2, 2u * lookups)) return TTF_ERR_GPOS;
    for (uint16_t i = 0; i < lookups; i++) {
        size_t lt = ll + u16(b, ll + 2 + 2u * i);
        if (!in(b, lt, 6)) return TTF_ERR_GPOS;
        if (u16(b, lt) != 2) continue; /* stb only reads pair adjustment lookups */
        uint16_t subtables = u16(b, lt + 4);
        if (!in(b, lt + 6, 2u * subtables)) return TTF_ERR_GPOS;
        for (uint16_t s = 0; s < subtables; s++) {
            size_t t = lt + u16(b, lt + 6 + 2u * s);
            if (!in(b, t, 8)) return TTF_ERR_GPOS;
            int32_t covered = coverage_count(b, t + u16(b, t + 2));
            if (covered < 0) return TTF_ERR_GPOS;
            uint16_t fmt = u16(b, t), vf1 = u16(b, t + 4), vf2 = u16(b, t + 6);
            if (vf1 != 4 || vf2 != 0) continue; /* stb returns 0 for other value formats */
            if (fmt == 1) {
                if (!in(b, t, 10)) return TTF_ERR_GPOS;
                uint16_t sets = u16(b, t + 8);
                if (!in(b, t + 10, 2u * sets)) return TTF_ERR_GPOS;
                /* stb reads the offset at the coverage index before comparing
                 * it with the set count */
                if (covered > sets && !in(b, t + 10, 2u * (size_t)covered)) return TTF_ERR_GPOS;
                for (uint16_t k = 0; k < sets; k++) {
                    size_t pv = t + u16(b, t + 10 + 2u * k);
                    if (!in(b, pv, 2) || !in(b, pv + 2, 4u * u16(b, pv))) return TTF_ERR_GPOS;
                }
            } else if (fmt == 2) {
                if (!in(b, t, 16)) return TTF_ERR_GPOS;
                if (!class_def_ok(b, t + u16(b, t + 8)) || !class_def_ok(b, t + u16(b, t + 10))) return TTF_ERR_GPOS;
                size_t n1 = u16(b, t + 12), n2 = u16(b, t + 14);
                if (!in(b, t + 16, 2u * n1 * n2)) return TTF_ERR_GPOS;
            }
        }
    }
    return TTF_OK;
}

/* The Unicode subtable stb_truetype uses: the last (3,1), (3,10) or (0,*)
 * record. Every glyph id it can return must be < glyphs: stb reads hmtx for
 * it without a bound check. */
static ttf_check_t check_cmap(const buf_t *b, table_t cmap, uint16_t glyphs) {
    const size_t c = cmap.off;
    if (cmap.len < 4) return TTF_ERR_CMAP;
    uint16_t records = u16(b, c + 2);
    if (!in(b, c + 4, 8u * records)) return TTF_ERR_CMAP;
    size_t m = 0;
    for (uint16_t i = 0; i < records; i++) {
        size_t rec = c + 4 + 8u * i;
        uint16_t plat = u16(b, rec), enc = u16(b, rec + 2);
        if ((plat == 3 && (enc == 1 || enc == 10)) || plat == 0) m = c + u32(b, rec + 4);
    }
    if (m == 0 || !in(b, m, 4)) return TTF_ERR_CMAP;

    switch (u16(b, m)) {
    case 0:
        if (!in(b, m, 6 + 256)) return TTF_ERR_CMAP;
        for (int i = 0; i < 256; i++) {
            if (b->d[m + 6 + i] >= glyphs) return TTF_ERR_CMAP;
        }
        return TTF_OK;
    case 6: {
        if (!in(b, m, 10)) return TTF_ERR_CMAP;
        uint16_t count = u16(b, m + 8);
        if (!in(b, m + 10, 2u * count)) return TTF_ERR_CMAP;
        for (uint16_t i = 0; i < count; i++) {
            if (u16(b, m + 10 + 2u * i) >= glyphs) return TTF_ERR_CMAP;
        }
        return TTF_OK;
    }
    case 4: {
        if (!in(b, m, 14)) return TTF_ERR_CMAP;
        uint16_t seg_x2 = u16(b, m + 6), range = u16(b, m + 8), selector = u16(b, m + 10), shift = u16(b, m + 12);
        uint16_t segs = seg_x2 / 2;
        if (seg_x2 == 0 || (seg_x2 & 1)) return TTF_ERR_CMAP;
        /* stb's binary search trusts these header fields */
        uint16_t pow2 = 1, log2 = 0;
        while ((uint32_t)pow2 * 2 <= segs) {
            pow2 *= 2;
            log2++;
        }
        if (range != 2 * pow2 || selector != log2 || shift != seg_x2 - range) return TTF_ERR_CMAP;
        const size_t ends = m + 14, starts = ends + seg_x2 + 2, deltas = starts + seg_x2, ranges = deltas + seg_x2;
        if (!in(b, ends, 4u * seg_x2 + 2)) return TTF_ERR_CMAP;
        for (uint16_t i = 0; i < segs; i++) {
            uint16_t end = u16(b, ends + 2u * i), start = u16(b, starts + 2u * i);
            int16_t delta = s16(b, deltas + 2u * i);
            uint16_t ro = u16(b, ranges + 2u * i);
            if (start > end) continue; /* never matches */
            if (ro == 0) {
                int32_t lo = (int32_t)start + delta, hi = (int32_t)end + delta;
                if (lo >= 0 && hi <= 0xFFFF) {
                    if (hi >= glyphs) return TTF_ERR_CMAP;
                    continue;
                }
                for (uint32_t ch = start; ch <= end; ch++) { /* wraps around 0 */
                    if (((ch + (uint32_t)(int32_t)delta) & 0xFFFFu) >= glyphs) return TTF_ERR_CMAP;
                }
            } else {
                /* stb: glyph id read at ranges + 2i + ro + 2 (ch - start), returned as is */
                for (uint32_t ch = start; ch <= end; ch++) {
                    size_t at = ranges + 2u * i + ro + 2u * (ch - start);
                    if (!in(b, at, 2) || u16(b, at) >= glyphs) return TTF_ERR_CMAP;
                }
            }
        }
        return TTF_OK;
    }
    case 12:
    case 13: {
        if (!in(b, m, 16)) return TTF_ERR_CMAP;
        uint32_t groups = u32(b, m + 12);
        if (groups > b->size / 12 || !in(b, m + 16, 12u * (size_t)groups)) return TTF_ERR_CMAP;
        bool fmt12 = u16(b, m) == 12;
        for (uint32_t i = 0; i < groups; i++) {
            size_t g = m + 16 + 12u * i;
            uint32_t start = u32(b, g), end = u32(b, g + 4), first = u32(b, g + 8);
            if (end < start) return TTF_ERR_CMAP;
            uint32_t last = fmt12 ? first + (end - start) : first;
            if (last < first || last >= glyphs) return TTF_ERR_CMAP;
        }
        return TTF_OK;
    }
    default:
        return TTF_ERR_CMAP; /* stb asserts on the rest */
    }
}

ttf_check_t ttf_check(const uint8_t *data, size_t size) {
    const buf_t buf = {data, size};
    const buf_t *b = &buf;
    if (!data || size < 12) return TTF_ERR_SHORT;
    uint32_t magic = u32(b, 0);
    if (magic != 0x00010000u && magic != 0x74727565u /* 'true' */) return TTF_ERR_MAGIC;
    uint16_t count = u16(b, 4);
    if (count == 0 || !in(b, 12, 16u * count)) return TTF_ERR_TABLES;
    for (uint16_t i = 0; i < count; i++) {
        size_t rec = 12 + 16u * i;
        if (!in(b, u32(b, rec + 8), u32(b, rec + 12))) return TTF_ERR_TABLES;
    }

    table_t cmap, head, hhea, hmtx, loca, glyf, maxp, gpos, kern;
    if (!find_table(b, count, "cmap", &cmap) || !find_table(b, count, "head", &head) ||
        !find_table(b, count, "hhea", &hhea) || !find_table(b, count, "hmtx", &hmtx) ||
        !find_table(b, count, "loca", &loca) || !find_table(b, count, "glyf", &glyf) ||
        !find_table(b, count, "maxp", &maxp))
        return TTF_ERR_MISSING;

    if (head.len < 54 || hhea.len < 36 || maxp.len < 6) return TTF_ERR_HEAD;
    uint16_t loca_fmt = u16(b, head.off + 50);
    uint16_t glyphs = u16(b, maxp.off + 4);
    uint16_t metrics = u16(b, hhea.off + 34);
    if (u16(b, head.off + 18) == 0 || loca_fmt > 1 || glyphs == 0) return TTF_ERR_HEAD;
    if (metrics == 0 || metrics > glyphs) return TTF_ERR_HEAD;
    if (hmtx.len < 4u * metrics + 2u * (size_t)(glyphs - metrics)) return TTF_ERR_HEAD;

    /* loca: glyphs + 1 offsets, non-decreasing, inside glyf */
    size_t entry = loca_fmt ? 4 : 2;
    if (loca.len < entry * ((size_t)glyphs + 1)) return TTF_ERR_LOCA;
    uint32_t prev = 0;
    for (uint32_t i = 0; i <= glyphs; i++) {
        uint32_t o = loca_fmt ? u32(b, loca.off + 4 * i) : 2u * u16(b, loca.off + 2 * i);
        if (o < prev || o > glyf.len) return TTF_ERR_LOCA;
        prev = o;
    }

    ttf_check_t r = check_cmap(b, cmap, glyphs);
    if (r != TTF_OK) return r;

    if (find_table(b, count, "GPOS", &gpos)) return check_gpos(b, gpos);
    if (find_table(b, count, "kern", &kern) && kern.len >= 4 && u16(b, kern.off + 2) >= 1) {
        /* stbtt__GetGlyphKernInfoAdvance: first subtable, format 0 */
        if (kern.len < 18) return TTF_ERR_TABLES;
        if (u16(b, kern.off + 8) == 1 && kern.len < 18 + 6u * u16(b, kern.off + 10)) return TTF_ERR_TABLES;
    }
    return TTF_OK;
}

const char *ttf_check_str(ttf_check_t r) {
    switch (r) {
    case TTF_OK: return "ok";
    case TTF_ERR_SHORT: return "too short";
    case TTF_ERR_MAGIC: return "not a TrueType font (CFF/OTF, collection or other)";
    case TTF_ERR_TABLES: return "table directory out of bounds";
    case TTF_ERR_MISSING: return "missing cmap/head/hhea/hmtx/loca/glyf/maxp";
    case TTF_ERR_HEAD: return "inconsistent head/hhea/maxp/hmtx";
    case TTF_ERR_LOCA: return "bad loca";
    case TTF_ERR_CMAP: return "no usable Unicode cmap";
    case TTF_ERR_GPOS: return "GPOS out of bounds";
    }
    return "?";
}
