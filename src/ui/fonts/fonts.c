//
// Created by Bolke de Bruin on 24/06/2024.
//
// The UI fonts: TrueType files in the media store (bucket `fonts`, see
// docs/MEDIA-STORE.md), rendered by LVGL's tiny_ttf (stb_truetype) straight
// from memory-mapped flash. Nothing is copied to RAM: each file is mapped
// once (media_open) and every size of that face reads the same mapping.
//
//   medium.ttf    fnt_medium_14/24/32/48/72
//   semibold.ttf  fnt_semibold_14/18
//   icons.ttf     Material Design Icons, one font per size, the fallback of
//                 the text fonts of that size
//
// Fallback chain of a text font: text -> icons (same size) -> LV_FONT_DEFAULT
// (LVGL's built-in Montserrat 14, the only compiled font). A missing or
// invalid text file makes its slots LV_FONT_DEFAULT; a missing icons file
// leaves the text fonts without icons.
//
// The files stay mapped until destroy_fonts(): the media store keeps the
// pages of a replaced or deleted font until then, so a font uploaded to the
// bucket takes effect at the next boot.
//

#include "fonts.h"

#include <stdbool.h>
#include <stddef.h>

#include "../../media/media_store.h"
#include "ttf_check.h"

lv_font_t *fnt_semibold_14;
lv_font_t *fnt_semibold_18;

lv_font_t *fnt_medium_14;
lv_font_t *fnt_medium_24;
lv_font_t *fnt_medium_32;
lv_font_t *fnt_medium_48;
lv_font_t *fnt_medium_72;

/* The one compiled font: LVGL's Montserrat 14 (ASCII plus LVGL's symbols). */
#define BUILTIN_FONT ((lv_font_t *)LV_FONT_DEFAULT)

typedef enum { FACE_MEDIUM, FACE_SEMIBOLD, FACE_ICONS, FACE_COUNT } face_t;

typedef struct {
    const char *key; /* in MEDIA_BUCKET_FONTS */
    lv_font_kerning_t kerning;
    media_file_t file;
    bool usable; /* mapped and passed ttf_check */
} face_slot_t;

/* Montserrat kerns through GPOS pair adjustment (lookup type 2, formats 1
 * and 2, x advance only), which stb_truetype reads; tools/fonts/check_fonts.py
 * verifies that its result equals the font's 'kern' feature. The icons have
 * no kerning; NONE also skips the per-glyph pair lookup. */
static face_slot_t faces[FACE_COUNT] = {
    [FACE_MEDIUM] = {"medium.ttf", LV_FONT_KERNING_NORMAL, {0}, false},
    [FACE_SEMIBOLD] = {"semibold.ttf", LV_FONT_KERNING_NORMAL, {0}, false},
    [FACE_ICONS] = {"icons.ttf", LV_FONT_KERNING_NONE, {0}, false},
};

/* Glyph cache per font (tiny_ttf caches that many glyphs: metrics and the
 * rendered A8 bitmap, in the LVGL pool), and the line box of the old fonts.
 *
 * Cache: 0 for every size. Measured on the device: a cached glyph holds
 * ~0.45 KB of the pool at 14 px, ~0.75 KB at 32 px, ~2 KB at 72 px, and the
 * pool (64 KB) has ~47 KB free at boot, which the page's widgets (~2 KB per
 * display box) and the render scratch below must share. A cache only helps
 * when it holds every glyph a refresh draws: LVGL renders the screen in
 * bands (20 lines main, 5 lines mini displays) and fetches each glyph again
 * for every band it crosses, in the same order, so an LRU cache smaller than
 * that working set misses on every fetch. The working sets (~30-60 glyphs at
 * 14 px on a page, ~10-16 digits and units per large size) would cost
 * 15-30 KB per size. Uncached, tiny_ttf renders a glyph in 0.6-1 ms from
 * mapped flash (the old .bin fonts, also uncached, took 1.6-5.3 ms) and
 * needs 3-7 KB of pool while it does (up to ~12 KB for a 72 px '@'), freed
 * right after: a value update in 72 px ("21.5°C", ~4 bands) re-renders in
 * ~30 ms. LV_TINY_TTF_CACHE_KERNING_CNT is 0 too: a GPOS pair lookup in
 * mapped flash is a few binary searches. Raise a size's count here only
 * with a pool measurement of the pages in use.
 *
 * Line box: tiny_ttf takes the line height from the font's hhea metrics
 * (1.22 em for Montserrat). For 14-32 px that is within 2 px of the old
 * .bin fonts; the old 48 and 72 px fonts held digits and units only, with a
 * 1.03 em line box, which page layouts were made with. Those two sizes keep
 * it (line_em_x100, base_em_x1000 below): numbers fit, a descender or an
 * accent above a capital is clipped by up to ~0.1 em. 0 = font metrics. */
typedef struct {
    lv_font_t **slot;
    face_t face;
    int32_t px;
    uint16_t cache;          /* glyphs */
    uint16_t line_em_x100;   /* line height, 1/100 em; 0 = from the font */
    uint16_t base_em_x1000;  /* baseline above the line bottom, 1/1000 em */
    lv_font_t *font;
} font_slot_t;

static font_slot_t font_slots[] = {
    {&fnt_semibold_14, FACE_SEMIBOLD, 14, 0, 0, 0, NULL},
    {&fnt_semibold_18, FACE_SEMIBOLD, 18, 0, 0, 0, NULL},
    {&fnt_medium_14, FACE_MEDIUM, 14, 0, 0, 0, NULL},
    {&fnt_medium_24, FACE_MEDIUM, 24, 0, 0, 0, NULL},
    {&fnt_medium_32, FACE_MEDIUM, 32, 0, 0, 0, NULL},
    {&fnt_medium_48, FACE_MEDIUM, 48, 0, 103, 155, NULL},
    {&fnt_medium_72, FACE_MEDIUM, 72, 0, 103, 155, NULL},
};
#define FONT_SLOT_COUNT (sizeof(font_slots) / sizeof(font_slots[0]))

/* One icons font per size the text fonts use. */
typedef struct {
    int32_t px;
    lv_font_t *font;
} icon_slot_t;

static icon_slot_t icon_slots[] = {{14, NULL}, {18, NULL}, {24, NULL}, {32, NULL}, {48, NULL}, {72, NULL}};
#define ICON_SLOT_COUNT (sizeof(icon_slots) / sizeof(icon_slots[0]))

static void open_face(face_slot_t *f) {
    media_err_t err = media_open(MEDIA_BUCKET_FONTS, f->key, &f->file);
    if (err != MEDIA_OK) {
        LV_LOG_WARN("font %s not loaded (media error %d)", f->key, (int)err);
        return;
    }
    ttf_check_t chk = ttf_check(f->file.data, f->file.size);
    if (chk != TTF_OK) {
        LV_LOG_WARN("font %s not loaded: %s", f->key, ttf_check_str(chk));
        media_close(&f->file);
        return;
    }
    f->usable = true;
}

static lv_font_t *create(face_t face, int32_t px, size_t cache) {
    face_slot_t *f = &faces[face];
    if (!f->usable) return NULL;
    return lv_tiny_ttf_create_data_ex(f->file.data, f->file.size, px, f->kerning, cache);
}

static lv_font_t *icons_for(int32_t px) {
    for (size_t i = 0; i < ICON_SLOT_COUNT; i++) {
        if (icon_slots[i].px == px) return icon_slots[i].font;
    }
    return NULL;
}

void init_fonts(void) {
    /* Idempotent; the store may already be up (S3 server). */
    media_init();
    for (size_t i = 0; i < FACE_COUNT; i++) open_face(&faces[i]);

    for (size_t i = 0; i < ICON_SLOT_COUNT; i++) {
        icon_slot_t *s = &icon_slots[i];
        s->font = create(FACE_ICONS, s->px, 0);
        if (s->font) s->font->fallback = BUILTIN_FONT;
    }

    for (size_t i = 0; i < FONT_SLOT_COUNT; i++) {
        font_slot_t *s = &font_slots[i];
        s->font = create(s->face, s->px, s->cache);
        if (!s->font) {
            if (faces[s->face].usable) LV_LOG_WARN("font %s %d px: out of memory", faces[s->face].key, (int)s->px);
            *s->slot = BUILTIN_FONT;
            continue;
        }
        if (s->line_em_x100) {
            s->font->line_height = (int32_t)((s->px * s->line_em_x100 + 50) / 100);
            s->font->base_line = (int32_t)((s->px * s->base_em_x1000 + 500) / 1000);
        }
        /* tiny_ttf leaves these 0; Montserrat's are ~0.1 em below the
         * baseline and ~0.05 em thick */
        s->font->underline_position = (int8_t)(-(s->px + 5) / 10);
        s->font->underline_thickness = (int8_t)((s->px + 10) / 20);
        lv_font_t *icons = icons_for(s->px);
        s->font->fallback = icons ? icons : BUILTIN_FONT;
        *s->slot = s->font;
    }
}

void destroy_fonts(void) {
    for (size_t i = 0; i < FONT_SLOT_COUNT; i++) {
        font_slot_t *s = &font_slots[i];
        *s->slot = BUILTIN_FONT;
        if (s->font) lv_tiny_ttf_destroy(s->font);
        s->font = NULL;
    }
    for (size_t i = 0; i < ICON_SLOT_COUNT; i++) {
        if (icon_slots[i].font) lv_tiny_ttf_destroy(icon_slots[i].font);
        icon_slots[i].font = NULL;
    }
    /* after the fonts: they read the mapping until destroyed */
    for (size_t i = 0; i < FACE_COUNT; i++) {
        media_close(&faces[i].file);
        faces[i].usable = false;
    }
}
