// The UI font registry: TrueType fonts in the media store (bucket `fonts`,
// docs/MEDIA-STORE.md, docs/FONTS.md) rendered by LVGL tiny_ttf straight
// from memory-mapped flash.
//
// A FACE is one TTF file plus an optional fallback face; it has no size.
// The two system faces always exist: FONT_FACE_SYSTEM "montserrat"
// (medium.ttf) and FONT_FACE_SYSTEM_BOLD "montserrat_semibold"
// (semibold.ttf). config.json "style.fonts" adds faces, and may give a
// system face a fallback and a lineheight (not another file)
// (fonts_configure()). Fallbacks are explicit only: a face without a
// configured fallback has none. No font name is special in the code.
//
// An INSTANCE is a face at one pixel size: an lv_font_t made on demand by
// fonts_get() and kept until reboot. Its address never changes (objects and
// styles keep the pointer); a change in the bucket reloads what it holds.
// Its fallback is the fallback face at the same px, and the end of every
// chain is LV_FONT_DEFAULT (LVGL's built-in Montserrat 12), so a glyph no
// face has still draws as LVGL's placeholder.
//
// Threads: init_fonts, destroy_fonts, fonts_configure and fonts_get run
// with the LVGL lock held (or before the LVGL task runs). fonts_find*,
// fonts_px and fonts_face_name read only the face table, which changes only
// in fonts_configure (boot, before scripts run): any task may call them.
#ifndef FONTS_H
#define FONTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

/* FONTS_MAX_FACES, FONTS_MAX_INSTANCES, FONT_NAME_MAX, FONT_SRC_MAX,
 * FONT_PX_MIN/MAX, FONT_CHAIN_MAX, the FONT_FACE_* indexes,
 * font_fallback_kind_t and font_def_t (one config.json "style.fonts" entry) are in
 * font_logic.h, which has no LVGL dependency. */
#include "font_logic.h"

/* Boot, before the LVGL task runs: the system faces (maps their files) and
 * the reload timer. Creates no instance. Idempotent. */
void init_fonts(void);

/* Destroy every instance (each keeps its address and then holds the
 * built-in font) and close the files. */
void destroy_fonts(void);

/* A file in bucket `fonts` changed (uploaded, replaced, deleted). Any task;
 * only records the request. The LVGL task reloads, instance by instance,
 * 1.5 s after the last request of a batch. */
void fonts_request_reload(void);

/* config.json "style.fonts" (LVGL lock held, once at boot, before any object
 * uses a config face). Adds faces in order (an entry with a system name sets
 * that face's id, fallback and lineheight instead; its src is ignored with a
 * warning), then resolves every fallback: unknown
 * references, chains longer than FONT_CHAIN_MAX and cycles are cut with a
 * warning. Entries beyond FONTS_MAX_FACES, with an invalid or duplicate
 * name or id, are skipped with a warning. Instances that already exist of
 * a changed face are reloaded. Returns the number of entries accepted. */
size_t fonts_configure(const font_def_t *defs, size_t count);

/* Face by name (system names included) or by config id; FONT_FACE_NONE
 * when unknown. `len` = bytes of `name` (not NUL-terminated is fine). */
int fonts_find(const char *name, size_t len);
int fonts_find_id(int32_t id);

/* The face's name ("" for an invalid face). */
const char *fonts_face_name(int face);

/* Config / script "fontsize" -> px: 1..5 = the legacy steps
 * 14/24/32/48/72; FONT_PX_MIN..FONT_PX_MAX = px; 0 or anything else =
 * `fallback_px` (font_logic_px). Items use style_logic_item_px, which also
 * has the title rule (step 2 = 18 px). */
int fonts_px(int fontsize, int fallback_px);

/* The instance of `face` at `px` (clamped to FONT_PX_MIN..MAX), made on
 * first use with its fallback chain. Never NULL: an invalid face, a full
 * instance table or a pool too short (< 4 KB would be left) give
 * LV_FONT_DEFAULT (warned once). LVGL lock held. */
lv_font_t *fonts_get(int face, int px);

/* Render statistics (POST /api/bench) since the last reset: glyphs
 * rasterized and glyph lookups (metrics + kerning), with their CPU cycles.
 * LVGL lock held. */
typedef struct {
    uint32_t glyphs;
    uint64_t glyph_cycles;
    uint32_t lookups;
    uint64_t lookup_cycles;
} fonts_stats_t;
void fonts_stats(fonts_stats_t *out, bool reset);

#ifdef __cplusplus
}
#endif

#endif
