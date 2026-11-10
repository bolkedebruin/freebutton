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
// invalid text file makes its slots the built-in font (still falling back to
// the icons); a missing icons file leaves the text fonts without icons.
//
// Online changes: fnt_* point at static lv_font_t wrappers that never move,
// because styles and objects keep the pointer (ui_box.c and ui_button.c
// styles, every label's local style). A wrapper holds a copy of a tiny_ttf
// font (or of the built-in font). After an upload or a delete in the bucket
// (fonts_request_reload(), from the S3 server) the LVGL task loads a new set
// from the bucket, copies it into the wrappers, refreshes every object of
// every display, then destroys the old set and closes its files; the store
// frees the pages of a replaced or deleted file at that close.
//
// Why copying a tiny_ttf lv_font_t is safe (LVGL 9.6 lv_tiny_ttf.c):
//  - the font's state is all behind font->dsc (ttf_font_desc_t: stb info,
//    its three caches, the data pointer), which has no pointer back to the
//    lv_font_t; the copy shares dsc, the original keeps owning it;
//  - the callbacks read only font->dsc, font->kerning and font->line_height
//    of the font LVGL passes them, which is the wrapper (lv_font_get_glyph_dsc
//    sets resolved_font to the font in the chain that has the glyph);
//  - its caches are keyed by codepoint / glyph index, not by font pointer
//    (and are off here, see below); LVGL keeps no cache keyed by font;
//  - lv_tiny_ttf_destroy() is only ever called on the original (it frees
//    dsc and that lv_font_t), after no wrapper points at its dsc;
//  - fallback pointers go wrapper -> wrapper (text -> icons of the size)
//    -> built-in font, so they stay valid across a swap.
// The swap runs inside lv_timer_handler() with the LVGL lock held: no
// refresh is in progress, so no glyph descriptor of the old set is in use.
//

#include "fonts.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

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
#define BUILTIN_FONT ((const lv_font_t *)LV_FONT_DEFAULT)

typedef enum { FACE_MEDIUM, FACE_SEMIBOLD, FACE_ICONS, FACE_COUNT } face_t;

/* Montserrat kerns through GPOS pair adjustment (lookup type 2, formats 1
 * and 2, x advance only), which stb_truetype reads; tools/fonts/check_fonts.py
 * verifies that its result equals the font's 'kern' feature. The icons have
 * no kerning; NONE also skips the per-glyph pair lookup. */
static const struct {
    const char *key; /* in MEDIA_BUCKET_FONTS */
    lv_font_kerning_t kerning;
} faces[FACE_COUNT] = {
    [FACE_MEDIUM] = {"medium.ttf", LV_FONT_KERNING_NORMAL},
    [FACE_SEMIBOLD] = {"semibold.ttf", LV_FONT_KERNING_NORMAL},
    [FACE_ICONS] = {"icons.ttf", LV_FONT_KERNING_NONE},
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
    uint16_t cache;         /* glyphs */
    uint16_t line_em_x100;  /* line height, 1/100 em; 0 = from the font */
    uint16_t base_em_x1000; /* baseline above the line bottom, 1/1000 em */
} text_slot_t;

static const text_slot_t text_slots[] = {
    {&fnt_semibold_14, FACE_SEMIBOLD, 14, 0, 0, 0},
    {&fnt_semibold_18, FACE_SEMIBOLD, 18, 0, 0, 0},
    {&fnt_medium_14, FACE_MEDIUM, 14, 0, 0, 0},
    {&fnt_medium_24, FACE_MEDIUM, 24, 0, 0, 0},
    {&fnt_medium_32, FACE_MEDIUM, 32, 0, 0, 0},
    {&fnt_medium_48, FACE_MEDIUM, 48, 0, 103, 155},
    {&fnt_medium_72, FACE_MEDIUM, 72, 0, 103, 155},
};
#define TEXT_COUNT (sizeof(text_slots) / sizeof(text_slots[0]))

/* One icons font per size the text fonts use. */
static const int32_t icon_px[] = {14, 18, 24, 32, 48, 72};
#define ICON_COUNT (sizeof(icon_px) / sizeof(icon_px[0]))

/* The stable fonts fnt_* point at. */
static lv_font_t text_wrap[TEXT_COUNT];
static lv_font_t icon_wrap[ICON_COUNT];

/* One loaded set: the mapped files and the tiny_ttf fonts made from them
 * (NULL = not loaded, the wrapper holds the built-in font). Two sets exist
 * only during a reload. */
typedef struct {
    media_file_t file[FACE_COUNT];
    bool usable[FACE_COUNT]; /* mapped and passed ttf_check */
    lv_font_t *text[TEXT_COUNT];
    lv_font_t *icons[ICON_COUNT];
} font_set_t;

static font_set_t sets[2];
static font_set_t *current; /* NULL before init_fonts / after destroy_fonts */

/* ---- loading ---------------------------------------------------------------- */

static void open_face(font_set_t *s, face_t face) {
    const char *key = faces[face].key;
    media_err_t err = media_open(MEDIA_BUCKET_FONTS, key, &s->file[face]);
    if (err != MEDIA_OK) {
        LV_LOG_WARN("font %s not loaded (media error %d)", key, (int)err);
        return;
    }
    ttf_check_t chk = ttf_check(s->file[face].data, s->file[face].size);
    if (chk != TTF_OK) {
        LV_LOG_WARN("font %s not loaded: %s", key, ttf_check_str(chk));
        media_close(&s->file[face]);
        return;
    }
    s->usable[face] = true;
}

static lv_font_t *create(font_set_t *s, face_t face, int32_t px, size_t cache) {
    if (!s->usable[face]) return NULL;
    lv_font_t *f = lv_tiny_ttf_create_data_ex(s->file[face].data, s->file[face].size, px, faces[face].kerning, cache);
    if (!f) LV_LOG_WARN("font %s %d px: out of memory", faces[face].key, (int)px);
    return f;
}

/* Map the files of the bucket and make every font. Same file as the set in
 * use: the store hands back the same mapping (one more reference). */
static void load_set(font_set_t *s) {
    memset(s, 0, sizeof(*s));
    for (int f = 0; f < FACE_COUNT; f++) open_face(s, (face_t)f);
    for (size_t i = 0; i < ICON_COUNT; i++) s->icons[i] = create(s, FACE_ICONS, icon_px[i], 0);
    for (size_t i = 0; i < TEXT_COUNT; i++) {
        const text_slot_t *t = &text_slots[i];
        s->text[i] = create(s, t->face, t->px, t->cache);
    }
}

/* Fonts first: they read the mappings until destroyed. */
static void release_set(font_set_t *s) {
    for (size_t i = 0; i < TEXT_COUNT; i++) {
        if (s->text[i]) lv_tiny_ttf_destroy(s->text[i]);
        s->text[i] = NULL;
    }
    for (size_t i = 0; i < ICON_COUNT; i++) {
        if (s->icons[i]) lv_tiny_ttf_destroy(s->icons[i]);
        s->icons[i] = NULL;
    }
    for (int f = 0; f < FACE_COUNT; f++) {
        media_close(&s->file[f]);
        s->usable[f] = false;
    }
}

static lv_font_t *icon_wrap_for(int32_t px) {
    for (size_t i = 0; i < ICON_COUNT; i++) {
        if (icon_px[i] == px) return &icon_wrap[i];
    }
    return NULL;
}

/* Point the wrappers at `s` (NULL: the built-in font everywhere). */
static void apply_set(const font_set_t *s) {
    for (size_t i = 0; i < ICON_COUNT; i++) {
        const lv_font_t *src = s && s->icons[i] ? s->icons[i] : BUILTIN_FONT;
        icon_wrap[i] = *src;
        /* the built-in font's own fallback is NULL; a missing icons file
         * makes this wrapper a second built-in font, which is harmless */
        icon_wrap[i].fallback = src == BUILTIN_FONT ? NULL : BUILTIN_FONT;
    }
    for (size_t i = 0; i < TEXT_COUNT; i++) {
        const text_slot_t *t = &text_slots[i];
        lv_font_t *w = &text_wrap[i];
        if (s && s->text[i]) {
            *w = *s->text[i];
            if (t->line_em_x100) {
                w->line_height = (int32_t)((t->px * t->line_em_x100 + 50) / 100);
                w->base_line = (int32_t)((t->px * t->base_em_x1000 + 500) / 1000);
            }
            /* tiny_ttf leaves these 0; Montserrat's are ~0.1 em below the
             * baseline and ~0.05 em thick */
            w->underline_position = (int8_t)(-(t->px + 5) / 10);
            w->underline_thickness = (int8_t)((t->px + 10) / 20);
        } else {
            *w = *BUILTIN_FONT;
        }
        lv_font_t *icons = s ? icon_wrap_for(t->px) : NULL;
        w->fallback = icons;
        *t->slot = w;
    }
}

/* Re-measure and redraw everything: what lv_obj_report_style_change(NULL)
 * does for the screens, plus the top/sys/bottom layers it skips (modals),
 * on every display (main and mini). lv_obj_refresh_style(ANY) sends
 * LV_EVENT_STYLE_CHANGED to the whole subtree (labels re-measure their
 * text) and invalidates it. */
static void refresh_all(void) {
    for (lv_display_t *d = lv_display_get_next(NULL); d; d = lv_display_get_next(d)) {
        lv_obj_t *roots[] = {lv_display_get_layer_bottom(d), lv_display_get_layer_top(d),
                             lv_display_get_layer_sys(d)};
        for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
            if (roots[i]) lv_obj_refresh_style(roots[i], LV_PART_ANY, LV_STYLE_PROP_ANY);
        }
        lv_obj_t *active = lv_display_get_screen_active(d);
        if (active) lv_obj_invalidate(active);
    }
    lv_obj_report_style_change(NULL); /* every screen of every display */
}

/* Room the new set needs in the LVGL pool while the old one still exists:
 * ~430 B per font (descriptor + three cache headers) x 13, with margin.
 * LVGL halts on some failed allocations inside tiny_ttf (lv_cache_create
 * asserts), so a reload waits for room instead of trying. */
#define RELOAD_POOL_NEEDED 8192u

/* false: not enough pool now, try again later */
static bool reload_fonts(void) {
    if (!current) return true;
    font_set_t *next = current == &sets[0] ? &sets[1] : &sets[0];
    lv_mem_monitor_t before;
    lv_mem_monitor(&before);
    if (before.free_size < RELOAD_POOL_NEEDED || before.free_biggest_size < 1024) {
        LV_LOG_WARN("fonts: reload postponed, LVGL pool free %u", (unsigned)before.free_size);
        return false;
    }
    load_set(next);
    apply_set(next);
    refresh_all();
    release_set(current); /* the store frees replaced/deleted pages here */
    current = next;
    lv_mem_monitor_t after;
    lv_mem_monitor(&after);
    LV_LOG_WARN("fonts reloaded: medium %s, semibold %s, icons %s; LVGL pool free %u -> %u",
                next->usable[FACE_MEDIUM] ? "ok" : "built-in", next->usable[FACE_SEMIBOLD] ? "ok" : "built-in",
                next->usable[FACE_ICONS] ? "ok" : "none", (unsigned)before.free_size, (unsigned)after.free_size);
    return true;
}

/* ---- reload requests -------------------------------------------------------- */

/* Bumped by fonts_request_reload() from any task; read by the LVGL task. */
static atomic_uint reload_requests;

#define RELOAD_POLL_MS 250u
#define RELOAD_QUIET_MS 1500u /* one reload after the last change of a batch */

static lv_timer_t *reload_timer;
static unsigned seen_requests;
static uint32_t last_change_tick;
static bool reload_pending;

void fonts_request_reload(void) { atomic_fetch_add_explicit(&reload_requests, 1u, memory_order_relaxed); }

/* LVGL task, inside lv_timer_handler(), lock held. */
static void reload_poll_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    unsigned n = atomic_load_explicit(&reload_requests, memory_order_relaxed);
    if (n != seen_requests) {
        seen_requests = n;
        last_change_tick = lv_tick_get();
        reload_pending = true;
        return;
    }
    if (reload_pending && lv_tick_elaps(last_change_tick) >= RELOAD_QUIET_MS) {
        if (reload_fonts()) {
            reload_pending = false;
        } else {
            last_change_tick = lv_tick_get(); /* retry after another quiet period */
        }
    }
}

/* ---- public API ----------------------------------------------------------------- */

void init_fonts(void) {
    if (current) return;
    /* Idempotent; the store may already be up (S3 server). */
    media_init();
    current = &sets[0];
    load_set(current);
    apply_set(current);
    seen_requests = atomic_load_explicit(&reload_requests, memory_order_relaxed);
    reload_pending = false;
    if (!reload_timer) reload_timer = lv_timer_create(reload_poll_cb, RELOAD_POLL_MS, NULL);
}

void destroy_fonts(void) {
    if (reload_timer) {
        lv_timer_delete(reload_timer);
        reload_timer = NULL;
    }
    apply_set(NULL); /* fnt_* keep their addresses: the built-in font */
    if (current) release_set(current);
    current = NULL;
}
