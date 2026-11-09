//
// Created by Bolke de Bruin on 24/06/2024.
//
// LVGL adapter for the streaming .bin fonts on LittleFS. The format itself is
// handled by font_bin.c; this file only maps it onto LVGL's public font API
// (lv_font_t callbacks, lv_font_glyph_dsc_t, lv_draw_buf_t, lv_fs_*).
//
// RAM per font: the stream_font_t below plus the cmap subtable headers
// (16 bytes each, 11-12 for our fonts). Glyph metrics, sparse cmap lists and
// kerning are read from the file on demand; bitmaps are decoded straight into
// LVGL's glyph draw buffer through a small stack buffer.
//

#include "fonts.h"

#include <string.h>

#include "font_bin.h"

LV_FONT_DECLARE(mnt_semibold_14);
LV_FONT_DECLARE(mnt_semibold_18);

LV_FONT_DECLARE(mnt_medium_14);
LV_FONT_DECLARE(mnt_medium_24);
LV_FONT_DECLARE(mnt_medium_32);
LV_FONT_DECLARE(mnt_medium_48);
LV_FONT_DECLARE(mnt_medium_72);

lv_font_t *fnt_semibold_14;
lv_font_t *fnt_semibold_18;

lv_font_t *fnt_medium_14;
lv_font_t *fnt_medium_24;
lv_font_t *fnt_medium_32;
lv_font_t *fnt_medium_48;
lv_font_t *fnt_medium_72;

typedef struct {
    lv_font_t font; /* font.dsc points back at this struct */
    font_bin_t bin;
    font_bin_cache_t cache;
    lv_fs_file_t file;
    bool file_open;
    const char *path; /* string literal, used to reopen the file lazily */
} stream_font_t;

/* ---- font_bin_io_t over lv_fs ------------------------------------------- */

/* The file is opened on first use, so fonts that are never drawn do not hold
 * a LittleFS handle (each one carries its own cache buffer). */
static bool ensure_open(stream_font_t *sf) {
    if (sf->file_open) return true;
    if (lv_fs_open(&sf->file, sf->path, LV_FS_MODE_RD) != LV_FS_RES_OK) return false;
    sf->file_open = true;
    return true;
}

static void close_file(stream_font_t *sf) {
    if (!sf->file_open) return;
    lv_fs_close(&sf->file);
    sf->file_open = false;
}

static bool io_read_at(void *ctx, uint32_t pos, void *dst, uint32_t len) {
    stream_font_t *sf = (stream_font_t *)ctx;
    if (!ensure_open(sf)) return false;
    uint32_t got = 0;
    if (lv_fs_seek(&sf->file, pos, LV_FS_SEEK_SET) != LV_FS_RES_OK) return false;
    if (lv_fs_read(&sf->file, dst, len, &got) != LV_FS_RES_OK) return false;
    return got == len;
}

static void *io_alloc(size_t size) { return lv_malloc(size); }
static void io_free(void *p) { lv_free(p); }

/* ---- lv_font_t callbacks -------------------------------------------------- */

static bool stream_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                                 uint32_t letter, uint32_t letter_next) {
    stream_font_t *sf = (stream_font_t *)font->dsc;

    /* Same as LVGL's own fonts: a tab is a space of double width. */
    bool is_tab = letter == '\t';
    if (is_tab) letter = ' ';

    uint16_t gid;
    font_bin_glyph_t g;
    if (!font_bin_lookup(&sf->bin, &sf->cache, letter, &gid, &g)) return false;

    int32_t adv = g.adv_w; /* 1/16 px */
    if (is_tab) adv *= 2;
    if (letter_next && sf->bin.kern_pos) {
        uint16_t gid_next;
        font_bin_glyph_t g_next;
        if (font_bin_lookup(&sf->bin, &sf->cache, letter_next, &gid_next, &g_next)) {
            adv += font_bin_kern(&sf->bin, gid, gid_next);
        }
    }
    if (adv < 0) adv = 0;

    dsc->adv_w = (uint16_t)((adv + 8) >> 4); /* round to whole pixels */
    dsc->box_w = is_tab ? g.box_w * 2 : g.box_w;
    dsc->box_h = g.box_h;
    dsc->ofs_x = g.ofs_x;
    dsc->ofs_y = g.ofs_y;
    dsc->stride = 0;
    /* Like fmt_txt fonts: report the source depth, get_glyph_bitmap always
     * hands back an A8 draw buffer. */
    dsc->format = (lv_font_glyph_format_t)sf->bin.bpp;
    dsc->is_placeholder = 0;
    dsc->gid.index = gid;
    dsc->entry = NULL; /* nothing to release */
    dsc->resolved_font = font;
    return true;
}

static const void *stream_get_glyph_bitmap(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf) {
    const lv_font_t *font = g_dsc->resolved_font;
    if (!font || !draw_buf || g_dsc->gid.index == 0) return NULL;
    stream_font_t *sf = (stream_font_t *)font->dsc;

    font_bin_glyph_t g;
    if (!font_bin_glyph_cached(&sf->bin, &sf->cache, (uint16_t)g_dsc->gid.index, &g)) return NULL;
    if (g.box_w == 0 || g.box_h == 0) return NULL;

    uint32_t stride = draw_buf->header.stride;
    if (stride < g.box_w || (uint64_t)stride * g.box_h > draw_buf->data_size) return NULL;

    if (!font_bin_decode(&sf->bin, &g, draw_buf->data, stride, true)) {
        LV_LOG_WARN("font: failed to decode glyph %u", (unsigned)g_dsc->gid.index);
        return NULL;
    }
    lv_draw_buf_flush_cache(draw_buf, NULL);
    return draw_buf;
}

/* ---- create / destroy ----------------------------------------------------- */

static void stream_font_destroy(stream_font_t *sf) {
    if (!sf) return;
    font_bin_close(&sf->bin);
    close_file(sf);
    lv_free(sf);
}

static stream_font_t *stream_font_create(const char *path, const lv_font_t *fallback) {
    stream_font_t *sf = (stream_font_t *)lv_malloc(sizeof(stream_font_t));
    if (!sf) return NULL;
    memset(sf, 0, sizeof(*sf));
    sf->path = path;

    const font_bin_io_t io = {io_read_at, io_alloc, io_free, sf};
    if (!font_bin_open(&sf->bin, &io)) {
        close_file(sf);
        lv_free(sf);
        return NULL;
    }
    /* Only the index is needed now; glyph reads reopen the file on demand. */
    close_file(sf);

    lv_font_t *font = &sf->font;
    font->dsc = sf;
    font->get_glyph_dsc = stream_get_glyph_dsc;
    font->get_glyph_bitmap = stream_get_glyph_bitmap;
    font->release_glyph = NULL; /* bitmaps live in LVGL's draw buffer */
    font->line_height = sf->bin.ascent - sf->bin.descent;
    font->base_line = -sf->bin.descent;
    font->subpx = sf->bin.subpx;
    font->kerning = LV_FONT_KERNING_NORMAL;
    font->static_bitmap = 0;
    /* lv_font_conv loses the underline position in .bin files (it writes the
     * thickness over it); Montserrat's is size / 10 below the baseline. */
    font->underline_position = (int8_t)(-(int32_t)(sf->bin.font_size + 5) / 10);
    font->underline_thickness = (int8_t)sf->bin.underline_thickness;
    /* If a glyph read fails later on (file gone), LVGL falls back to the
     * compiled copy of the same font. */
    font->fallback = fallback;

    LV_LOG_INFO("font %s: %u glyphs, index RAM %u bytes", path, (unsigned)sf->bin.glyph_count,
                (unsigned)(sizeof(stream_font_t) + sf->bin.cmap_count * sizeof(font_bin_cmap_t)));
    return sf;
}

/* ---- public API ------------------------------------------------------------ */

typedef struct {
    const char *path;
    const lv_font_t *builtin;
    lv_font_t **slot;
    stream_font_t *stream;
} font_slot_t;

static font_slot_t font_slots[] = {
    {"S:/fonts/mnt_semibold_14.bin", &mnt_semibold_14, &fnt_semibold_14, NULL},
    {"S:/fonts/mnt_semibold_18.bin", &mnt_semibold_18, &fnt_semibold_18, NULL},
    {"S:/fonts/mnt_medium_14.bin", &mnt_medium_14, &fnt_medium_14, NULL},
    {"S:/fonts/mnt_medium_24.bin", &mnt_medium_24, &fnt_medium_24, NULL},
    {"S:/fonts/mnt_medium_32.bin", &mnt_medium_32, &fnt_medium_32, NULL},
    {"S:/fonts/mnt_medium_48.bin", &mnt_medium_48, &fnt_medium_48, NULL},
    {"S:/fonts/mnt_medium_72.bin", &mnt_medium_72, &fnt_medium_72, NULL},
};

#define FONT_SLOT_COUNT (sizeof(font_slots) / sizeof(font_slots[0]))

void init_fonts(void) {
    for (size_t i = 0; i < FONT_SLOT_COUNT; i++) {
        font_slot_t *s = &font_slots[i];
        s->stream = stream_font_create(s->path, s->builtin);
        if (s->stream) {
            *s->slot = &s->stream->font;
        } else {
            LV_LOG_WARN("font %s not loaded, using built-in", s->path);
            *s->slot = (lv_font_t *)s->builtin;
        }
    }
}

void destroy_fonts(void) {
    for (size_t i = 0; i < FONT_SLOT_COUNT; i++) {
        font_slot_t *s = &font_slots[i];
        *s->slot = (lv_font_t *)s->builtin;
        stream_font_destroy(s->stream);
        s->stream = NULL;
    }
}
