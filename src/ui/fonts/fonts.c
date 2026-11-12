//
// Created by Bolke de Bruin on 24/06/2024.
//
// The UI font registry (API and model: fonts.h; pure rules: font_logic.c).
// TrueType files in the media store (bucket `fonts`, docs/MEDIA-STORE.md,
// docs/FONTS.md) are rendered by LVGL's tiny_ttf (stb_truetype) straight from
// memory-mapped flash. Nothing is copied to RAM: each face's file is mapped
// once (media_open) and every size of that face reads the same mapping.
//
//   faces      the two system faces, montserrat / montserrat_semibold
//              (medium.ttf, semibold.ttf), plus config.json "style.fonts"
//              (fonts_configure)
//   instances  a face at one px, made on first use by fonts_get(), kept
//              until reboot in a static table at a stable address
//
// Fallback chain of an instance: its face's fallback face at the same px
// (only as config sets it: no face has one by default) -> ... ->
// LV_FONT_DEFAULT (LVGL's built-in Montserrat 12, the only compiled font).
// Chains are acyclic and at most FONT_CHAIN_MAX faces long
// (font_logic_resolve), so building one recurses at most that deep. A
// missing or invalid file makes its instances a copy of the built-in font
// that keeps its fallback (text without the file still shows the glyphs of
// its fallback faces).
//
// Online changes: callers keep the address of an instance's wrapper
// (inst[].wrap), because styles and objects keep the pointer (the shared
// element styles, every label's local style). A wrapper holds a copy of a
// tiny_ttf font (or of the built-in font). After an upload or a delete in the
// bucket (fonts_request_reload(), from the S3 server) the LVGL task re-opens
// every face's file; an unchanged file gives back the same mapping, a
// replaced one a new mapping. Each instance of a changed face then gets a
// new tiny_ttf made from the new mapping, copied into its wrapper, and the
// old tiny_ttf is destroyed: one extra instance of LVGL pool at a time.
// Every display is restyled, and a face's previous file is closed once no
// instance reads it any more (the store frees the pages of a replaced or
// deleted file at that close).
//
// Why copying a tiny_ttf lv_font_t is safe (LVGL 9.6 lv_tiny_ttf.c):
//  - the font's state is all behind font->dsc (ttf_font_desc_t: stb info,
//    its three caches, the data pointer), which has no pointer back to the
//    lv_font_t; the copy shares dsc, the original (inst[].ttf) keeps owning
//    it;
//  - the callbacks read only font->dsc, font->kerning and font->line_height
//    of the font LVGL passes them, which is the wrapper (lv_font_get_glyph_dsc
//    sets resolved_font to the font in the chain that has the glyph); the
//    wrapper's own line box (line_height, base_line) is what labels lay out;
//  - its caches are keyed by codepoint / glyph index, not by font pointer
//    (and are off here, see below); LVGL keeps no cache keyed by font;
//  - lv_tiny_ttf_destroy() is only ever called on the original (it frees
//    dsc and that lv_font_t), right after its wrapper got the new copy;
//  - fallback pointers go wrapper -> wrapper -> built-in font and are kept
//    across a swap, so they stay valid.
// The swap runs inside lv_timer_handler() (or fonts_configure, at boot) with
// the LVGL lock held: no refresh is in progress, so no glyph descriptor of an
// old font is in use.
//

#include "fonts.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "../../media/media_store.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "src/draw/lv_draw_buf_private.h" /* lv_draw_buf_handlers_t fields */
#include "ttf_check.h"

/* The one compiled font: LVGL's Montserrat 12 (ASCII plus LVGL's symbols). */
#define BUILTIN_FONT ((const lv_font_t *)LV_FONT_DEFAULT)

/* ---- faces -------------------------------------------------------------------- */

typedef struct {
    const char *name;  /* a font_logic.c literal or in string_pool */
    const char *key;   /* in MEDIA_BUCKET_FONTS; same */
    int32_t id;        /* 0 = none */
    media_file_t file; /* the version new instances are made from; handle 0 = none (missing / invalid) */
    media_file_t prev; /* the version before, while an instance still reads it */
    uint16_t lineheight_x100;
    int8_t fallback;   /* face index or FONT_FACE_NONE */
    bool usable;       /* `file` is mapped and passed ttf_check */
} face_t;

static face_t faces[FONTS_MAX_FACES];
static size_t face_count; /* 0 until the system faces are set up */

/* Names and keys of config faces (font_logic_merge keeps within it). */
#define STRING_POOL 512u
static char string_pool[STRING_POOL];
static size_t string_used;

/* ---- instances ------------------------------------------------------------------ */

/* Glyph cache per font: 0 (tiny_ttf caches that many glyphs: metrics and
 * the rendered A8 bitmap, in the LVGL pool).
 *
 * Measured on the device: a cached glyph holds ~0.45 KB of the pool at 14 px,
 * ~0.75 KB at 32 px, ~2 KB at 72 px, and the pool (64 KB) has ~47 KB free at
 * boot, which the page's widgets (~2 KB per display box) and the render
 * scratch below must share. A cache only helps when it holds every glyph a
 * refresh draws: LVGL renders the screen in bands (20 lines main, 5 lines
 * mini displays) and fetches each glyph again for every band it crosses, in
 * the same order, so an LRU cache smaller than that working set misses on
 * every fetch. The working sets (~30-60 glyphs at 14 px on a page, ~10-16
 * digits and units per large size) would cost 15-30 KB per size. Uncached,
 * tiny_ttf renders a glyph in 0.6-1 ms from mapped flash (the old .bin
 * fonts, also uncached, took 1.6-5.3 ms) and needs 3-7 KB of pool while it
 * does (up to ~12 KB for a 72 px '@'), freed right after: a value update in
 * 72 px ("21.5°C", ~4 bands) re-renders in ~30 ms. LV_TINY_TTF_CACHE_KERNING_CNT
 * is 0 too: a GPOS pair lookup in mapped flash is a few binary searches.
 * Use a cache only with a pool measurement of the pages in use.
 *
 * Line box (font_logic_line_box): tiny_ttf takes the line height from the
 * font's hhea metrics (1.22 em for Montserrat). For 14-32 px that is within
 * 2 px of the old .bin fonts; the old 48 and 72 px fonts held digits and
 * units only, with a 1.03 em line box, which page layouts were made with.
 * FONT_FACE_SYSTEM keeps that box at 48 px and up (numbers fit, a descender
 * or an accent above a capital is clipped by up to ~0.1 em) unless config
 * gives it a lineheight; a config lineheight applies at every px. */
#define GLYPH_CACHE 0

typedef struct {
    lv_font_t wrap;    /* what callers point at; never moves */
    lv_font_t *ttf;    /* the tiny_ttf font wrap copies; NULL = wrap is the built-in font */
    uint32_t handle;   /* media handle of the file ttf reads; 0 with ttf NULL */
    int8_t face;       /* FONT_FACE_NONE: orphaned by a second fonts_configure */
    uint8_t px;
    bool fallback_missing; /* the fallback could not be made (budget): retried by fonts_get */
} instance_t;

static instance_t inst[FONTS_MAX_INSTANCES];
static size_t inst_count;

static bool started; /* init_fonts done, files open */

/* stb_truetype rasterizes a glyph with temporary allocations (up to ~12 KB
 * at 72 px, all freed before the glyph is returned). They come from the
 * system heap, not the LVGL pool: tools/scripts/render_build.py compiles
 * lv_tiny_ttf.c (and only that file) with lv_malloc / lv_free renamed to
 * fb_tiny_ttf_malloc / fb_tiny_ttf_free. While guarded_get_bitmap rasterizes
 * (`rasterizing`, LVGL task with the lock held) they use the heap; at any
 * other time they pass straight to lv_malloc / lv_free, so the font
 * descriptors tiny_ttf allocates stay in the pool. The bitmap path of
 * lv_tiny_ttf.c allocates nothing but stb scratch and stb frees every block
 * before it returns, so no block crosses the window.
 *
 * Why: the pool (LV_MEM_SIZE, ~12 KB free on a busy device) had to keep the
 * worst case of the largest glyph free at all times, so a few more font
 * sizes (~430 B each) left glyphs without room (they disappeared). The heap
 * is shared but has ~58 KB free; the scratch is held for ~1 ms per glyph.
 *
 * The guard renders only when the heap keeps FONT_HEAP_RESERVE after this
 * glyph's worst case (font_logic_glyph_fits); otherwise LVGL skips this one
 * glyph ("Couldn't get the bitmap of a glyph") until its next redraw. A
 * failed stb allocation must not happen: stb dereferences some results
 * unchecked (and STBTT_assert is a no-op, lv_conf.h).
 * The rendered glyph bitmap itself (A8) is not stb scratch: see
 * font_buf_malloc. */
void *fb_tiny_ttf_malloc(size_t size);
void fb_tiny_ttf_free(void *p);

static bool rasterizing;
static int32_t scratch_live; /* heap blocks stb holds now (0 outside the window) */

void *fb_tiny_ttf_malloc(size_t size) {
    if (!rasterizing) return lv_malloc(size);
    void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p) scratch_live++;
    return p;
}

void fb_tiny_ttf_free(void *p) {
    if (!rasterizing) {
        lv_free(p);
        return;
    }
    if (p) {
        scratch_live--;
        heap_caps_free(p);
    }
}

static const void *(*tiny_get_bitmap)(lv_font_glyph_dsc_t *, lv_draw_buf_t *);

/* fonts_stats(): glyphs rasterized and their CPU cycles (LVGL task only). */
static uint32_t stat_glyphs;
static uint64_t stat_cycles;


static uint32_t skipped_glyphs;

static const void *guarded_get_bitmap(lv_font_glyph_dsc_t *g, lv_draw_buf_t *buf) {
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t heap_free = heap_caps_get_free_size(caps);
    const size_t heap_biggest = heap_caps_get_largest_free_block(caps);
    if (!font_logic_glyph_fits(g->box_w, g->box_h, heap_free, heap_biggest)) {
        if ((skipped_glyphs++ & 63u) == 0) {
            LV_LOG_WARN("fonts: glyph skipped (%lu so far): heap %lu B free, biggest %lu B, need %lu B + %u reserve",
                        (unsigned long)skipped_glyphs, (unsigned long)heap_free, (unsigned long)heap_biggest,
                        (unsigned long)font_logic_glyph_scratch(g->box_w, g->box_h), FONT_HEAP_RESERVE);
        }
        return NULL;
    }
    rasterizing = true;
    const uint32_t c0 = esp_cpu_get_cycle_count();
    const void *bitmap = tiny_get_bitmap(g, buf);
    stat_cycles += esp_cpu_get_cycle_count() - c0;
    stat_glyphs++;
    rasterizing = false;
    if (scratch_live != 0) { /* cannot happen: stb frees what it allocates */
        LV_LOG_ERROR("fonts: %ld stb scratch blocks not freed", (long)scratch_live);
        scratch_live = 0;
    }
    return bitmap;
}

/* A wrapper of a tiny_ttf font renders through the guard. */
/* Glyph lookups (metrics, kerning) timed for fonts_stats(); a lookup that
 * falls back to another font is counted once (`dsc_depth`). */
static bool (*tiny_get_dsc)(const lv_font_t *, lv_font_glyph_dsc_t *, uint32_t, uint32_t);
static uint32_t stat_dsc;
static uint64_t stat_dsc_cycles;
static int dsc_depth;

static bool timed_get_dsc(const lv_font_t *f, lv_font_glyph_dsc_t *g, uint32_t letter, uint32_t next) {
    if (dsc_depth++) {
        const bool r = tiny_get_dsc(f, g, letter, next);
        dsc_depth--;
        return r;
    }
    const uint32_t c0 = esp_cpu_get_cycle_count();
    const bool r = tiny_get_dsc(f, g, letter, next);
    stat_dsc_cycles += esp_cpu_get_cycle_count() - c0;
    stat_dsc++;
    dsc_depth--;
    return r;
}

void fonts_stats(fonts_stats_t *out, bool reset) {
    if (out) {
        out->glyphs = stat_glyphs;
        out->glyph_cycles = stat_cycles;
        out->lookups = stat_dsc;
        out->lookup_cycles = stat_dsc_cycles;
    }
    if (reset) {
        stat_glyphs = 0;
        stat_cycles = 0;
        stat_dsc = 0;
        stat_dsc_cycles = 0;
    }
}

static void guard(lv_font_t *w) {
    tiny_get_bitmap = w->get_glyph_bitmap; /* the same tiny_ttf callback for every font */
    w->get_glyph_bitmap = guarded_get_bitmap;
    tiny_get_dsc = w->get_glyph_dsc;
    w->get_glyph_dsc = timed_get_dsc;
}

/* (Re)fill a wrapper from its instance's ttf (NULL: the built-in font),
 * keeping its fallback. */
static void fill_wrap(instance_t *in) {
    lv_font_t *w = &in->wrap;
    const lv_font_t *fallback = w->fallback;
    if (in->ttf) {
        *w = *in->ttf;
        guard(w);
        const int face = in->face;
        const uint16_t lh = face >= 0 ? faces[face].lineheight_x100 : 0;
        font_logic_line_box(in->px, lh, face == FONT_FACE_SYSTEM, &w->line_height, &w->base_line);
        /* tiny_ttf leaves these 0; Montserrat's are ~0.1 em below the
         * baseline and ~0.05 em thick (every face gets them) */
        w->underline_position = (int8_t)(-(in->px + 5) / 10);
        w->underline_thickness = (int8_t)((in->px + 10) / 20);
        if (!fallback) fallback = BUILTIN_FONT;
    } else {
        *w = *BUILTIN_FONT;
        /* the built-in font falling back to itself would be a wasted lookup */
        if (fallback == BUILTIN_FONT) fallback = NULL;
    }
    w->fallback = fallback;
}

/* Kerning: always LV_FONT_KERNING_NORMAL. Montserrat kerns through GPOS
 * pair adjustment (lookup type 2, formats 1 and 2, x advance only), which
 * stb_truetype reads; tiny_ttf turns kerning off by itself for a font
 * without kern / GPOS tables (Material Design Icons has none). */
static lv_font_t *make_ttf(int face, int px) {
    const face_t *fc = &faces[face];
    lv_font_t *f = lv_tiny_ttf_create_data_ex(fc->file.data, fc->file.size, px, LV_FONT_KERNING_NORMAL, GLYPH_CACHE);
    if (!f) LV_LOG_WARN("fonts: %s %d px: out of memory", fc->name, px);
    return f;
}

/* Room in the LVGL pool for one more tiny_ttf instance. LVGL halts on some
 * failed allocations inside tiny_ttf (lv_cache_create asserts), so nothing
 * is tried without room. */
static font_budget_t budget(size_t used, size_t max) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    return font_logic_budget(used, max, m.free_size, m.free_biggest_size);
}

static uint32_t refused;

static void refuse(int face, int px, font_budget_t why) {
    if ((refused++ & 63u) == 0) {
        LV_LOG_WARN("fonts: %s %d px not made (%s; %lu so far): the built-in font is used",
                    face >= 0 && (size_t)face < face_count ? faces[face].name : "?", px,
                    why == FONT_BUDGET_TABLE_FULL ? "instance table full" : "LVGL pool low",
                    (unsigned long)refused);
    }
}

static lv_font_t *get(int face, int px);

/* The fallback of `face` at `px`; *missing when it exists but could not be
 * made now. */
static const lv_font_t *fallback_for(int face, int px, bool *missing) {
    *missing = false;
    const int fb = faces[face].fallback;
    if (fb < 0) return BUILTIN_FONT;
    lv_font_t *f = get(fb, px);
    if (f == BUILTIN_FONT) *missing = true; /* get() gives the built-in font only on failure */
    return f;
}

static void relink(instance_t *in) {
    bool missing;
    const lv_font_t *fb = fallback_for(in->face, in->px, &missing);
    in->fallback_missing = missing;
    in->wrap.fallback = fb;
    fill_wrap(in);
}

static lv_font_t *get(int face, int px) {
    for (size_t i = 0; i < inst_count; i++) {
        instance_t *in = &inst[i];
        if (in->face == face && in->px == px) {
            if (in->fallback_missing) relink(in);
            return &in->wrap;
        }
    }
    if (inst_count >= FONTS_MAX_INSTANCES) {
        refuse(face, px, FONT_BUDGET_TABLE_FULL);
        return (lv_font_t *)BUILTIN_FONT;
    }
    /* the chain first (deepest first), then this face */
    bool missing;
    const lv_font_t *fb = fallback_for(face, px, &missing);
    if (inst_count >= FONTS_MAX_INSTANCES) {
        refuse(face, px, FONT_BUDGET_TABLE_FULL); /* the chain took the last slots */
        return (lv_font_t *)BUILTIN_FONT;
    }
    lv_font_t *ttf = NULL;
    if (faces[face].usable) {
        const font_budget_t b = budget(inst_count, FONTS_MAX_INSTANCES);
        if (b != FONT_BUDGET_OK) {
            refuse(face, px, b);
            return (lv_font_t *)BUILTIN_FONT;
        }
        ttf = make_ttf(face, px);
        if (!ttf) {
            refuse(face, px, FONT_BUDGET_POOL_LOW);
            return (lv_font_t *)BUILTIN_FONT;
        }
    }
    instance_t *in = &inst[inst_count++];
    memset(in, 0, sizeof(*in));
    in->face = (int8_t)face;
    in->px = (uint8_t)px;
    in->ttf = ttf;
    in->handle = ttf ? faces[face].file.handle : 0;
    in->fallback_missing = missing;
    in->wrap.fallback = fb;
    fill_wrap(in);
    return &in->wrap;
}

/* ---- loading / reloading ----------------------------------------------------------- */

static bool file_in_use(int face, uint32_t handle) {
    if (!handle) return false;
    for (size_t i = 0; i < inst_count; i++) {
        if (inst[i].face == face && inst[i].handle == handle) return true;
    }
    return false;
}

/* Bring the faces' files and every instance in line with the bucket.
 * Returns false when something waits for pool room (retry later);
 * *updated = instances that changed. `verbose`: also warn about files that
 * are still missing (boot, configure). */
static bool sync_fonts(bool verbose, size_t *updated) {
    bool complete = true;
    *updated = 0;

    /* 1. every face's file */
    for (size_t f = 0; f < face_count; f++) {
        face_t *fc = &faces[f];
        media_file_t nf;
        memset(&nf, 0, sizeof(nf));
        const media_err_t err = media_open(MEDIA_BUCKET_FONTS, fc->key, &nf);
        if (err == MEDIA_OK && nf.handle == fc->file.handle) {
            media_close(&nf); /* unchanged: drop the extra reference */
            continue;
        }
        if (err != MEDIA_OK && !fc->file.handle) {
            if (verbose) LV_LOG_WARN("fonts: %s: %s not loaded (media error %d)", fc->name, fc->key, (int)err);
            continue; /* still none */
        }
        /* changed. At most two versions per face: finish moving the
         * instances off the oldest one first (next retry). */
        if (fc->prev.handle) {
            if (file_in_use((int)f, fc->prev.handle)) {
                if (err == MEDIA_OK) media_close(&nf);
                complete = false;
                continue;
            }
            media_close(&fc->prev);
        }
        bool ok = false;
        if (err != MEDIA_OK) {
            LV_LOG_WARN("fonts: %s: %s not loaded (media error %d)", fc->name, fc->key, (int)err);
        } else {
            const ttf_check_t chk = ttf_check(nf.data, nf.size);
            if (chk != TTF_OK) {
                LV_LOG_WARN("fonts: %s: %s not loaded: %s", fc->name, fc->key, ttf_check_str(chk));
                media_close(&nf);
            } else {
                ok = true;
            }
        }
        if (fc->file.handle) {
            if (file_in_use((int)f, fc->file.handle)) {
                fc->prev = fc->file;
            } else {
                media_close(&fc->file);
            }
        }
        if (ok) {
            fc->file = nf;
        } else {
            memset(&fc->file, 0, sizeof(fc->file));
        }
        fc->usable = ok;
    }

    /* 2. every instance onto its face's current file, one at a time */
    for (size_t i = 0; i < inst_count; i++) {
        instance_t *in = &inst[i];
        if (in->face < 0) continue;
        const face_t *fc = &faces[in->face];
        if (in->handle == fc->file.handle) continue; /* up to date (0 == 0: built-in either way) */
        lv_font_t *nt = NULL;
        if (fc->usable) {
            if (budget(0, 1) != FONT_BUDGET_OK) {
                complete = false;
                continue;
            }
            nt = make_ttf(in->face, in->px);
            if (!nt) {
                complete = false;
                continue;
            }
        }
        lv_font_t *old = in->ttf;
        in->ttf = nt;
        in->handle = nt ? fc->file.handle : 0;
        fill_wrap(in);
        if (old) lv_tiny_ttf_destroy(old); /* nothing points at its dsc any more */
        (*updated)++;
    }

    /* 3. previous versions nobody reads any more (the store frees replaced
     * or deleted pages here) */
    for (size_t f = 0; f < face_count; f++) {
        face_t *fc = &faces[f];
        if (fc->prev.handle && !file_in_use((int)f, fc->prev.handle)) media_close(&fc->prev);
    }
    return complete;
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

static void retry_later(void) {
    reload_pending = true;
    last_change_tick = lv_tick_get();
}

/* false: not everything could be reloaded (pool short), try again later */
static bool reload_fonts(void) {
    if (!started) return true;
    lv_mem_monitor_t before;
    lv_mem_monitor(&before);
    size_t updated;
    const bool complete = sync_fonts(false, &updated);
    if (updated) refresh_all();
    lv_mem_monitor_t after;
    lv_mem_monitor(&after);
    LV_LOG_WARN("fonts reloaded: %u of %u instances updated%s; LVGL pool free %u -> %u", (unsigned)updated,
                (unsigned)inst_count, complete ? "" : ", the rest waits for pool room",
                (unsigned)before.free_size, (unsigned)after.free_size);
    return complete;
}

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

/* Glyph bitmaps (LVGL's font draw buffers: tiny_ttf renders each glyph into
 * one, uncached, and LVGL frees it after drawing the letter) come from the
 * internal heap instead of the LVGL pool: they live only while one letter is
 * drawn, and the pool is what a busy page fills up. A failed allocation makes
 * tiny_ttf return no bitmap: that glyph is skipped, nothing aborts. */
static void *font_buf_malloc(size_t size, lv_color_format_t cf) {
    (void)cf;
    return heap_caps_malloc(size + LV_DRAW_BUF_ALIGN - 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void font_buf_free(void *buf) { heap_caps_free(buf); }

static void setup_system_faces(void) {
    font_face_spec_t spec[FONT_FACE_BUILTIN_COUNT];
    const size_t n = font_logic_builtins(spec);
    font_logic_resolve(spec, n, NULL, NULL);
    for (size_t f = 0; f < n; f++) {
        face_t *fc = &faces[f];
        memset(fc, 0, sizeof(*fc));
        fc->name = spec[f].name;
        fc->key = spec[f].src;
        fc->fallback = spec[f].fallback;
    }
    face_count = n;
}

void init_fonts(void) {
    if (started) return;
    lv_draw_buf_handlers_t *fh = lv_draw_buf_get_font_handlers();
    fh->buf_malloc_cb = font_buf_malloc;
    fh->buf_free_cb = font_buf_free;
    /* Idempotent; the store may already be up (S3 server). */
    media_init();
    if (!face_count) setup_system_faces();
    started = true;
    size_t updated;
    const bool complete = sync_fonts(true, &updated); /* maps the files; instances only after a destroy_fonts */
    seen_requests = atomic_load_explicit(&reload_requests, memory_order_relaxed);
    reload_pending = false;
    if (!complete) retry_later();
    if (!reload_timer) reload_timer = lv_timer_create(reload_poll_cb, RELOAD_POLL_MS, NULL);
}

void destroy_fonts(void) {
    if (reload_timer) {
        lv_timer_delete(reload_timer);
        reload_timer = NULL;
    }
    /* instances keep their addresses: the built-in font (and their fallback) */
    for (size_t i = 0; i < inst_count; i++) {
        instance_t *in = &inst[i];
        lv_font_t *old = in->ttf;
        in->ttf = NULL;
        in->handle = 0;
        fill_wrap(in);
        if (old) lv_tiny_ttf_destroy(old);
    }
    for (size_t f = 0; f < face_count; f++) {
        media_close(&faces[f].file);
        media_close(&faces[f].prev);
        faces[f].usable = false;
    }
    started = false;
}

static void config_warn(void *ctx, font_warn_t code, const char *name) {
    (void)ctx;
    LV_LOG_WARN("fonts: config font \"%.24s\": %s", name ? name : "", font_logic_warn_str(code));
}

static const char *pool_dup(const char *s) {
    const size_t n = strlen(s) + 1;
    if (string_used + n > STRING_POOL) return ""; /* cannot happen: font_logic_merge keeps within it */
    char *d = &string_pool[string_used];
    memcpy(d, s, n);
    string_used += n;
    return d;
}

size_t fonts_configure(const font_def_t *defs, size_t count) {
    if (!face_count) setup_system_faces();
    if (!defs) count = 0;

    font_face_spec_t spec[FONTS_MAX_FACES];
    size_t n = font_logic_builtins(spec);
    const size_t accepted =
        font_logic_merge(spec, &n, FONTS_MAX_FACES, defs, count, STRING_POOL, NULL, config_warn, NULL);
    font_logic_resolve(spec, n, config_warn, NULL);

    /* A second call: instances of config faces lose their face (its index
     * may name another font now); they keep their address and fallback and
     * hold the built-in font. Then the config faces' files are closed. */
    for (size_t i = 0; i < inst_count; i++) {
        instance_t *in = &inst[i];
        if (in->face < FONT_FACE_BUILTIN_COUNT) continue;
        lv_font_t *old = in->ttf;
        in->ttf = NULL;
        in->handle = 0;
        in->face = FONT_FACE_NONE;
        in->fallback_missing = false;
        fill_wrap(in);
        if (old) lv_tiny_ttf_destroy(old);
    }
    for (size_t f = FONT_FACE_BUILTIN_COUNT; f < face_count; f++) {
        media_close(&faces[f].file);
        media_close(&faces[f].prev);
        memset(&faces[f], 0, sizeof(faces[f]));
    }

    /* The new table. A system face keeps its file (its key is fixed).
     * Strings from `defs` go to the pool; the system ones are literals. */
    string_used = 0;
    for (size_t f = 0; f < n; f++) {
        face_t *fc = &faces[f];
        const bool builtin = f < FONT_FACE_BUILTIN_COUNT;
        fc->name = builtin ? spec[f].name : pool_dup(spec[f].name);
        fc->key = builtin ? spec[f].src : pool_dup(spec[f].src);
        fc->id = spec[f].id;
        fc->lineheight_x100 = spec[f].lineheight_x100;
        fc->fallback = spec[f].fallback;
    }
    face_count = n;

    if (started) {
        size_t updated;
        const size_t existing = inst_count;
        const bool complete = sync_fonts(true, &updated);
        /* new fallbacks and line boxes for instances made before (normally
         * none: nothing uses a font before the config is read) */
        for (size_t i = 0; i < existing; i++) {
            if (inst[i].face >= 0) relink(&inst[i]);
        }
        if (!complete) retry_later();
        if (existing) refresh_all();
    }
    LV_LOG_WARN("fonts: %u of %u config fonts accepted, %u faces", (unsigned)accepted, (unsigned)count,
                (unsigned)face_count);
    return accepted;
}

int fonts_find(const char *name, size_t len) {
    if (!name || len == 0) return FONT_FACE_NONE;
    for (size_t f = 0; f < face_count; f++) {
        const char *n = faces[f].name;
        if (strlen(n) == len && memcmp(n, name, len) == 0) return (int)f;
    }
    return FONT_FACE_NONE;
}

int fonts_find_id(int32_t id) {
    if (id < 1) return FONT_FACE_NONE;
    for (size_t f = 0; f < face_count; f++) {
        if (faces[f].id == id) return (int)f;
    }
    return FONT_FACE_NONE;
}

const char *fonts_face_name(int face) {
    return face >= 0 && (size_t)face < face_count ? faces[face].name : "";
}

int fonts_px(int fontsize, int fallback_px) { return font_logic_px(fontsize, fallback_px); }


lv_font_t *fonts_get(int face, int px) {
    if (face < 0 || (size_t)face >= face_count) return (lv_font_t *)BUILTIN_FONT;
    if (px < FONT_PX_MIN) px = FONT_PX_MIN;
    if (px > FONT_PX_MAX) px = FONT_PX_MAX;
    return get(face, px);
}
