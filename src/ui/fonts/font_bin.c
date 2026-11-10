//
// Streaming reader for lv_font_conv "--format bin" fonts. See font_bin.h.
//
// File layout (all little endian, every table starts with uint32 size + 4 char label):
//   head  font metrics and the bit widths used in glyf
//   cmap  uint32 count, count * 16 byte subtable headers, subtable data
//   loca  uint32 count, count offsets (uint16 or uint32) relative to glyf start
//   glyf  per glyph a bit stream (MSB first): adv, x, y, w, h, then the bitmap
//   kern  optional: uint8 format + 3 pad, then pairs (0) or a class matrix (3)
//
// Kept compatible with C++ so the native tests can #include this file.
//
#include "font_bin.h"

#include <string.h>

/* ---- little endian helpers ---------------------------------------------- */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool rd(const font_bin_t *f, uint32_t pos, void *dst, uint32_t len) {
    return f->io.read_at(f->io.ctx, pos, dst, len);
}

static bool rd8(const font_bin_t *f, uint32_t pos, uint8_t *v) { return rd(f, pos, v, 1); }

static bool rd16(const font_bin_t *f, uint32_t pos, uint16_t *v) {
    uint8_t b[2];
    if (!rd(f, pos, b, 2)) return false;
    *v = le16(b);
    return true;
}

/* Check a table label at `pos` and return its total length (0 on error). */
static uint32_t table_len(const font_bin_t *f, uint32_t pos, const char *label) {
    uint8_t b[8];
    if (!rd(f, pos, b, 8) || memcmp(b + 4, label, 4) != 0) return 0;
    uint32_t len = le32(b);
    return len >= 8 ? len : 0;
}

/* ---- bit reader --------------------------------------------------------- */

/* MSB-first bit reader over [pos, end) of the file, refilled in small chunks
 * so decoding a large glyph never needs a heap buffer. */
typedef struct {
    const font_bin_t *f;
    uint32_t pos;
    uint32_t end;
    uint32_t acc;  /* pending bits, right aligned */
    uint8_t nbits; /* valid bits in acc */
    uint8_t idx;
    uint8_t len;
    bool err;
    uint8_t buf[64];
} bit_reader_t;

static void br_init(bit_reader_t *r, const font_bin_t *f, uint32_t pos, uint32_t end) {
    r->f = f;
    r->pos = pos;
    r->end = end;
    r->acc = 0;
    r->nbits = 0;
    r->idx = 0;
    r->len = 0;
    r->err = false;
}

/* Read n <= 16 bits. Running past the record sets err and yields zeros. */
static uint32_t br_bits(bit_reader_t *r, uint8_t n) {
    while (r->nbits < n) {
        if (r->idx == r->len) {
            uint32_t left = r->end - r->pos;
            uint32_t chunk = left < sizeof(r->buf) ? left : (uint32_t)sizeof(r->buf);
            if (r->err || chunk == 0 || !rd(r->f, r->pos, r->buf, chunk)) {
                r->err = true;
                return 0;
            }
            r->pos += chunk;
            r->len = (uint8_t)chunk;
            r->idx = 0;
        }
        r->acc = (r->acc << 8) | r->buf[r->idx++];
        r->nbits += 8;
    }
    r->nbits -= n;
    return (r->acc >> r->nbits) & ((1u << n) - 1u);
}

static int32_t br_signed(bit_reader_t *r, uint8_t n) {
    uint32_t v = br_bits(r, n);
    if (n && (v & (1u << (n - 1)))) return (int32_t)v - (int32_t)(1u << n);
    return (int32_t)v;
}

/* ---- open / close --------------------------------------------------------- */

static bool parse_head(font_bin_t *f, uint32_t len) {
    uint8_t h[40];
    if (len < 8 + sizeof(h) || !rd(f, 8, h, sizeof(h))) return false;
    if (le32(h + 0) != 1) return false; /* version */
    uint16_t tables = le16(h + 4);
    f->font_size = le16(h + 6);
    f->ascent = le16(h + 8);
    f->descent = (int16_t)le16(h + 10);
    /* h+12..h+21: typo metrics and min/max y, not needed for rendering */
    f->default_adv = le16(h + 22);
    f->kern_scale = le16(h + 24);
    f->loca_32 = h[26];
    f->gid_16 = h[27];
    f->adv_fp = h[28];
    f->bpp = h[29];
    f->xy_bits = h[30];
    f->wh_bits = h[31];
    f->adv_bits = h[32];
    f->compression = h[33];
    f->subpx = h[34];
    /* lv_font_conv writes the underline thickness over the position field
     * (h+36), so only the thickness survives in the file. */
    f->underline_thickness = le16(h + 36);

    if (tables < 3) return false;
    if (f->bpp != 1 && f->bpp != 2 && f->bpp != 3 && f->bpp != 4 && f->bpp != 8) return false;
    if (f->compression > 2) return false;
    /* font_bin_glyph_t keeps boxes in 8 bits; adv is at most 16 bits. */
    if (f->xy_bits > 8 || f->wh_bits > 8 || f->adv_bits > 16) return false;
    if (f->adv_bits == 0 && f->default_adv == 0) return false;
    return true;
}

static bool parse_cmap(font_bin_t *f, uint32_t start, uint32_t len) {
    uint8_t b[4];
    if (!rd(f, start + 8, b, 4)) return false;
    uint32_t count = le32(b);
    if (count == 0 || count > 0xFFFF || 12 + count * 16 > len) return false;

    f->cmaps = (font_bin_cmap_t *)f->io.alloc(count * sizeof(font_bin_cmap_t));
    if (!f->cmaps) return false;
    f->cmap_count = (uint16_t)count;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t s[16];
        if (!rd(f, start + 12 + i * 16, s, sizeof(s))) return false;
        font_bin_cmap_t *c = &f->cmaps[i];
        c->data_pos = start + le32(s + 0); /* offset is relative to the cmap table */
        c->range_start = le32(s + 4);
        c->range_length = le16(s + 8);
        c->glyph_id_start = le16(s + 10);
        c->entries = le16(s + 12);
        c->type = s[14];
        if (c->type > FONT_BIN_CMAP_SPARSE_TINY) return false;
    }
    return true;
}

static bool parse_kern(font_bin_t *f, uint32_t start, uint32_t len) {
    uint8_t b[4];
    if (len < 16 || !rd(f, start + 8, b, 1)) return false;
    f->kern_format = b[0];
    f->kern_pos = start + 12; /* after size, label, format and 3 pad bytes */
    if (!rd(f, f->kern_pos, b, 4)) return false;

    if (f->kern_format == 0) {
        f->kern_pair_count = le32(b);
        uint32_t need = 4 + f->kern_pair_count * ((f->gid_16 ? 4u : 2u) + 1u);
        return 12 + need <= len;
    }
    if (f->kern_format == 3) {
        f->kern_map_len = le16(b);
        f->kern_left_classes = b[2];
        f->kern_right_classes = b[3];
        uint32_t need = 4 + 2u * f->kern_map_len + (uint32_t)b[2] * b[3];
        return 12 + need <= len;
    }
    return false;
}

bool font_bin_open(font_bin_t *f, const font_bin_io_t *io) {
    memset(f, 0, sizeof(*f));
    f->io = *io;

    uint32_t head_len = table_len(f, 0, "head");
    if (!head_len || !parse_head(f, head_len)) return false;

    uint16_t tables;
    if (!rd16(f, 12, &tables)) return false;

    uint32_t cmap_start = head_len;
    uint32_t cmap_len = table_len(f, cmap_start, "cmap");
    if (!cmap_len || !parse_cmap(f, cmap_start, cmap_len)) goto fail;

    {
        uint32_t loca_start = cmap_start + cmap_len;
        uint32_t loca_len = table_len(f, loca_start, "loca");
        uint8_t b[4];
        if (!loca_len || !rd(f, loca_start + 8, b, 4)) goto fail;
        uint32_t count = le32(b);
        if (count < 2 || count > 0xFFFF || 12 + count * (f->loca_32 ? 4u : 2u) > loca_len) goto fail;
        f->glyph_count = (uint16_t)count;
        f->loca_pos = loca_start + 12;

        f->glyf_pos = loca_start + loca_len;
        f->glyf_len = table_len(f, f->glyf_pos, "glyf");
        if (!f->glyf_len) goto fail;
    }

    if (tables >= 4) {
        uint32_t kern_start = f->glyf_pos + f->glyf_len;
        uint32_t kern_len = table_len(f, kern_start, "kern");
        if (!kern_len || !parse_kern(f, kern_start, kern_len)) goto fail;
    }
    return true;

fail:
    font_bin_close(f);
    return false;
}

void font_bin_close(font_bin_t *f) {
    if (f->cmaps) f->io.free(f->cmaps);
    f->cmaps = NULL;
    f->cmap_count = 0;
}

/* ---- cmap ------------------------------------------------------------------- */

/* Binary search a sorted uint16 list in the file. Returns index or -1. */
static int32_t find_u16(const font_bin_t *f, uint32_t pos, uint16_t count, uint16_t key) {
    int32_t lo = 0, hi = (int32_t)count - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        uint16_t v;
        if (!rd16(f, pos + 2u * (uint32_t)mid, &v)) return -1;
        if (v == key) return mid;
        if (v < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

uint16_t font_bin_glyph_id(const font_bin_t *f, uint32_t codepoint) {
    for (uint16_t i = 0; i < f->cmap_count; i++) {
        const font_bin_cmap_t *c = &f->cmaps[i];
        uint32_t rcp = codepoint - c->range_start; /* wraps for codepoint < start */
        if (rcp >= c->range_length) continue;

        switch (c->type) {
            case FONT_BIN_CMAP_FORMAT0_TINY:
                return (uint16_t)(c->glyph_id_start + rcp);
            case FONT_BIN_CMAP_FORMAT0_FULL: {
                uint8_t d;
                if (!rd8(f, c->data_pos + rcp, &d)) return 0;
                /* lv_font_conv stores delta 0 for holes; only the first code
                 * point of the range really maps to delta 0. */
                return (d || rcp == 0) ? (uint16_t)(c->glyph_id_start + d) : 0;
            }
            case FONT_BIN_CMAP_SPARSE_TINY:
            case FONT_BIN_CMAP_SPARSE_FULL: {
                int32_t idx = find_u16(f, c->data_pos, c->entries, (uint16_t)rcp);
                if (idx < 0) return 0;
                if (c->type == FONT_BIN_CMAP_SPARSE_TINY) return (uint16_t)(c->glyph_id_start + idx);
                uint16_t d;
                /* the glyph id deltas follow the code point list */
                if (!rd16(f, c->data_pos + 2u * c->entries + 2u * (uint32_t)idx, &d)) return 0;
                return (uint16_t)(c->glyph_id_start + d);
            }
            default:
                return 0;
        }
    }
    return 0;
}

/* ---- glyphs ----------------------------------------------------------------- */

bool font_bin_glyph(const font_bin_t *f, uint16_t gid, font_bin_glyph_t *out) {
    if (gid == 0 || gid >= f->glyph_count) return false;

    /* Start and end of the record: this loca entry and the next one, or the
     * end of glyf for the last glyph. One read covers both entries. */
    uint8_t b[8];
    uint32_t start, end;
    bool last = gid + 1u >= f->glyph_count;
    if (f->loca_32) {
        if (!rd(f, f->loca_pos + 4u * gid, b, last ? 4 : 8)) return false;
        start = le32(b);
        end = last ? f->glyf_len : le32(b + 4);
    } else {
        if (!rd(f, f->loca_pos + 2u * gid, b, last ? 2 : 4)) return false;
        start = le16(b);
        end = last ? f->glyf_len : le16(b + 2);
    }
    if (start < 8 || end < start || end > f->glyf_len || end - start > 0xFFFF) return false;

    /* The header is at most 16 + 4 * 8 bits, so 6 bytes. */
    uint32_t hdr_end = f->glyf_pos + (end - start < 6 ? end : start + 6);
    bit_reader_t r;
    br_init(&r, f, f->glyf_pos + start, hdr_end);

    uint32_t adv = f->adv_bits ? br_bits(&r, f->adv_bits) : f->default_adv;
    if (!f->adv_fp) adv *= 16;
    out->ofs_x = (int8_t)br_signed(&r, f->xy_bits);
    out->ofs_y = (int8_t)br_signed(&r, f->xy_bits);
    out->box_w = (uint8_t)br_bits(&r, f->wh_bits);
    out->box_h = (uint8_t)br_bits(&r, f->wh_bits);
    if (r.err || adv > 0xFFFF) return false;

    out->adv_w = (uint16_t)adv;
    out->pos = f->glyf_pos + start;
    out->len = (uint16_t)(end - start);
    return true;
}

/* ---- kerning ---------------------------------------------------------------- */

int8_t font_bin_kern_raw(const font_bin_t *f, uint16_t left, uint16_t right) {
    if (!f->kern_pos) return 0;
    uint8_t v = 0;

    if (f->kern_format == 3) {
        /* uint16 map_len, uint8 left/right class count, left map, right map,
         * int8 values[left][right]. Class 0 means "no kerning". */
        if (left >= f->kern_map_len || right >= f->kern_map_len) return 0;
        uint8_t lc, rc;
        uint32_t maps = f->kern_pos + 4;
        if (!rd8(f, maps + left, &lc) || lc == 0) return 0;
        if (!rd8(f, maps + f->kern_map_len + right, &rc) || rc == 0) return 0;
        if (lc > f->kern_left_classes || rc > f->kern_right_classes) return 0;
        uint32_t values = maps + 2u * f->kern_map_len;
        if (!rd8(f, values + (uint32_t)(lc - 1) * f->kern_right_classes + (rc - 1), &v)) return 0;
        return (int8_t)v;
    }

    /* Format 0: uint32 count, pairs sorted by (left, right), then values. */
    uint32_t pair_size = f->gid_16 ? 4 : 2;
    uint32_t pairs = f->kern_pos + 4;
    uint32_t key = ((uint32_t)left << 16) | right;
    int32_t lo = 0, hi = (int32_t)f->kern_pair_count - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        uint8_t p[4];
        if (!rd(f, pairs + (uint32_t)mid * pair_size, p, pair_size)) return 0;
        uint32_t k = f->gid_16 ? ((uint32_t)le16(p) << 16) | le16(p + 2) : ((uint32_t)p[0] << 16) | p[1];
        if (k == key) {
            if (!rd8(f, pairs + f->kern_pair_count * pair_size + (uint32_t)mid, &v)) return 0;
            return (int8_t)v;
        }
        if (k < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

int32_t font_bin_kern(const font_bin_t *f, uint16_t left, uint16_t right) {
    return ((int32_t)font_bin_kern_raw(f, left, right) * f->kern_scale) >> 4;
}

/* Class matrix kerning: the classes of a glyph (0 = none). */
static void kern_classes(const font_bin_t *f, uint16_t gid, uint8_t *lc, uint8_t *rc) {
    *lc = *rc = 0;
    if (!f->kern_pos || f->kern_format != 3 || gid >= f->kern_map_len) return;
    uint32_t maps = f->kern_pos + 4;
    if (!rd8(f, maps + gid, lc) || *lc > f->kern_left_classes) *lc = 0;
    if (!rd8(f, maps + f->kern_map_len + gid, rc) || *rc > f->kern_right_classes) *rc = 0;
}

int32_t font_bin_kern_entries(const font_bin_t *f, const font_bin_cache_entry_t *left,
                              const font_bin_cache_entry_t *right) {
    if (!f->kern_pos) return 0;
    if (f->kern_format != 3) return font_bin_kern(f, left->gid, right->gid);
    if (!left->kern_left || !right->kern_right) return 0;
    uint8_t v;
    uint32_t values = f->kern_pos + 4 + 2u * f->kern_map_len;
    if (!rd8(f, values + (uint32_t)(left->kern_left - 1) * f->kern_right_classes + (right->kern_right - 1), &v))
        return 0;
    return ((int32_t)(int8_t)v * f->kern_scale) >> 4;
}

/* ---- bitmap decoding ----------------------------------------------------- */

/* Writes decoded pixels row by row into the strided output. */
typedef struct {
    uint8_t *row;
    uint32_t stride;
    uint16_t x, w;
    uint32_t left; /* pixels still to write */
} pixel_sink_t;

static void put_run(pixel_sink_t *s, uint8_t v, uint32_t n) {
    if (n > s->left) n = s->left; /* a corrupt run must not overflow out */
    s->left -= n;
    while (n--) {
        s->row[s->x++] = v;
        if (s->x == s->w) {
            s->x = 0;
            s->row += s->stride;
        }
    }
}

/* lv_font_conv's RLE (compress.js), viewed as runs:
 *  - a pixel is a bpp-bit literal;
 *  - a literal equal to the previous literal starts a repeat: it is one pixel,
 *    followed by up to 10 '1' bits (one more pixel each) closed by a '0';
 *  - 11 '1' bits instead switch to a 6-bit counter: the repeat is 11 + counter;
 *  - after a repeat the next literal starts fresh (never compared). */
static void decode_rle(bit_reader_t *r, pixel_sink_t *s, uint8_t bpp) {
    bool fresh = true;
    uint8_t prev = 0;
    while (s->left && !r->err) {
        uint8_t v = (uint8_t)br_bits(r, bpp);
        if (fresh || v != prev) {
            put_run(s, v, 1);
            prev = v;
            fresh = false;
            continue;
        }
        uint32_t ones = 0;
        while (ones < 11 && br_bits(r, 1)) ones++;
        uint32_t n = ones < 11 ? 1 + ones : 11 + br_bits(r, 6);
        put_run(s, v, n);
        fresh = true;
    }
}

static const uint8_t opa1[2] = {0, 255};
static const uint8_t opa2[4] = {0, 85, 170, 255};
static const uint8_t opa3[8] = {0, 36, 73, 109, 146, 182, 218, 255}; /* as LVGL */
static const uint8_t opa4[16] = {0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255};

bool font_bin_decode(const font_bin_t *f, const font_bin_glyph_t *g, uint8_t *out, uint32_t stride, bool a8) {
    uint32_t w = g->box_w, h = g->box_h;
    if (w == 0 || h == 0) return true;
    if (stride < w) return false;

    /* The bitmap follows the header bits directly, not byte aligned. */
    bit_reader_t r;
    br_init(&r, f, g->pos, g->pos + g->len);
    br_bits(&r, f->adv_bits);
    br_bits(&r, f->xy_bits);
    br_bits(&r, f->xy_bits);
    br_bits(&r, f->wh_bits);
    br_bits(&r, f->wh_bits);

    pixel_sink_t s = {out, stride, 0, (uint16_t)w, w * h};
    if (f->compression == 0) {
        while (s.left && !r.err) put_run(&s, (uint8_t)br_bits(&r, f->bpp), 1);
    } else {
        decode_rle(&r, &s, f->bpp);
    }
    if (r.err) return false;

    /* Prefilter: each stored line is XORed with the line above it. */
    if (f->compression == 1) {
        for (uint32_t y = 1; y < h; y++) {
            uint8_t *row = out + y * stride;
            const uint8_t *above = row - stride;
            for (uint32_t x = 0; x < w; x++) row[x] ^= above[x];
        }
    }

    if (a8 && f->bpp != 8) {
        const uint8_t *opa = f->bpp == 1 ? opa1 : f->bpp == 2 ? opa2 : f->bpp == 3 ? opa3 : opa4;
        for (uint32_t y = 0; y < h; y++) {
            uint8_t *row = out + y * stride;
            for (uint32_t x = 0; x < w; x++) row[x] = opa[row[x]];
        }
    }
    return true;
}

/* ---- cache -------------------------------------------------------------------- */

bool font_bin_lookup(const font_bin_t *f, font_bin_cache_t *c, uint32_t codepoint,
                     uint16_t *gid, font_bin_glyph_t *glyph) {
    font_bin_cache_entry_t e;
    if (!font_bin_lookup_entry(f, c, codepoint, &e)) return false;
    *gid = e.gid;
    *glyph = e.glyph;
    return true;
}

bool font_bin_lookup_entry(const font_bin_t *f, font_bin_cache_t *c, uint32_t codepoint,
                           font_bin_cache_entry_t *out) {
    font_bin_cache_entry_t hit;
    uint8_t i;
    for (i = 0; i < c->used; i++) {
        if (c->e[i].codepoint == codepoint) break;
    }

    if (i < c->used) {
        hit = c->e[i];
    } else {
        hit.codepoint = codepoint;
        hit.gid = font_bin_glyph_id(f, codepoint);
        if (hit.gid && !font_bin_glyph(f, hit.gid, &hit.glyph)) {
            return false; /* read error: do not cache, a retry may work */
        }
        if (!hit.gid) memset(&hit.glyph, 0, sizeof(hit.glyph));
        kern_classes(f, hit.gid, &hit.kern_left, &hit.kern_right);
        if (c->used < FONT_BIN_CACHE_SIZE) c->used++;
        i = (uint8_t)(c->used - 1); /* drop the last (least recently used) entry */
    }

    /* move to front */
    memmove(&c->e[1], &c->e[0], i * sizeof(c->e[0]));
    c->e[0] = hit;

    if (!hit.gid) return false;
    *out = hit;
    return true;
}

bool font_bin_glyph_cached(const font_bin_t *f, const font_bin_cache_t *c, uint16_t gid, font_bin_glyph_t *glyph) {
    for (uint8_t i = 0; i < c->used; i++) {
        if (c->e[i].gid == gid) {
            *glyph = c->e[i].glyph;
            return true;
        }
    }
    return font_bin_glyph(f, gid, glyph);
}
