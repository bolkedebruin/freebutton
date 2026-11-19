// The pure rules of the font registry (src/ui/fonts/fonts.c): config entry
// validation, the face table merge (the two system faces + config.json
// "fonts"), fallback resolution with the cycle / depth cut, the
// size step -> px mapping, the instance budget and the line box.
//
// Plain C, no LVGL: the native tests build it (test/test_font_logic). The
// constants and config types here are part of the fonts.h API (fonts.h
// includes this header); they live here so that code without LVGL (tests,
// config parsing) can use them.
#ifndef FONT_LOGIC_H
#define FONT_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONTS_MAX_FACES 8        /* the 2 system faces + up to 6 from config */
#define FONTS_MAX_INSTANCES 24   /* face x px pairs, fallbacks included */
#define FONT_NAME_MAX 24         /* docs/NAMES.md: [A-Za-z_][A-Za-z0-9_]{0,23} */
#define FONT_SRC_MAX 96          /* a key in bucket `fonts` (MEDIA_KEY_MAX) */
#define FONT_PX_MIN 6
#define FONT_PX_MAX 72           /* larger glyphs need more rasterizer pool than the guard allows */
#define FONT_CHAIN_MAX 4         /* faces in one fallback chain, the first included */

/* The system faces: always present, first in the face table; config faces
 * follow. -1 = no face. Names and files (bucket `fonts`, which refuses to
 * delete these two files: S3Logic.h mediaKeyDeleteProtected): */
enum { FONT_FACE_SYSTEM = 0, FONT_FACE_SYSTEM_BOLD = 1, FONT_FACE_BUILTIN_COUNT = 2 };
#define FONT_FACE_NONE (-1)
#define FONT_SYSTEM_NAME "montserrat"
#define FONT_SYSTEM_SRC "medium.ttf"
#define FONT_SYSTEM_BOLD_NAME "montserrat_semibold"
#define FONT_SYSTEM_BOLD_SRC "semibold.ttf"

/* Fallbacks are explicit only: no face has one unless config gives it. */
typedef enum {
    FONT_FALLBACK_DEFAULT = 0, /* "fallback" absent: none */
    FONT_FALLBACK_NONE,        /* "fallback": "" */
    FONT_FALLBACK_NAME,        /* "fallback": "<name>" */
    FONT_FALLBACK_ID,          /* "fallback": <id> */
} font_fallback_kind_t;

/* One entry of config.json "fonts". */
typedef struct {
    char name[FONT_NAME_MAX + 1];
    int32_t id;                   /* 0 = none; else >= 1 */
    char src[FONT_SRC_MAX + 1];   /* a new face's file; must be "" for a system name (else warned, ignored) */
    font_fallback_kind_t fallback_kind;
    char fallback_name[FONT_NAME_MAX + 1];
    int32_t fallback_id;
    uint16_t lineheight_x100;     /* line box in 1/100 em (80..200); 0 = the font's metrics */
} font_def_t;

/* ---- validation ------------------------------------------------------------ */

#define FONT_LINEHEIGHT_MIN 80
#define FONT_LINEHEIGHT_MAX 200

/* docs/NAMES.md: 1..FONT_NAME_MAX of [A-Za-z0-9_], not starting with a digit. */
bool font_logic_valid_name(const char *name, size_t len);
/* A key in bucket `fonts`: 1..FONT_SRC_MAX of [A-Za-z0-9._-], ending in
 * ".ttf" or ".otf" with at least one character before it (NUL-terminated). */
bool font_logic_valid_src(const char *src);
/* 0 (font metrics) or FONT_LINEHEIGHT_MIN..MAX. */
bool font_logic_valid_lineheight(uint16_t lineheight_x100);

/* ---- the face table -------------------------------------------------------- */

/* One face as the merge sees it. The strings point at the system literals
 * or into the caller's font_def_t array (valid as long as that array). */
typedef struct {
    const char *name;
    const char *src;             /* key in bucket `fonts` */
    int32_t id;                  /* 0 = none */
    uint16_t lineheight_x100;    /* 0 = font metrics */
    font_fallback_kind_t fallback_kind;
    const char *fallback_name;   /* FONT_FALLBACK_NAME */
    int32_t fallback_id;         /* FONT_FALLBACK_ID */
    int8_t fallback;             /* resolved by font_logic_resolve(): face index or FONT_FACE_NONE */
} font_face_spec_t;

typedef enum {
    FONT_WARN_BAD_NAME = 0,      /* entry skipped */
    FONT_WARN_BAD_ID,            /* entry skipped (id < 0) */
    FONT_WARN_DUP_NAME,          /* entry skipped */
    FONT_WARN_DUP_ID,            /* entry skipped */
    FONT_WARN_BAD_SRC,           /* entry skipped (a new face needs a valid src) */
    FONT_WARN_BAD_LINEHEIGHT,    /* entry kept, lineheight ignored */
    FONT_WARN_TOO_MANY,          /* entry skipped: FONTS_MAX_FACES or the string room is used up */
    FONT_WARN_UNKNOWN_FALLBACK,  /* face kept without a fallback */
    FONT_WARN_CYCLE,             /* the link that closes a cycle is cut */
    FONT_WARN_TOO_DEEP,          /* the link past FONT_CHAIN_MAX faces is cut */
    FONT_WARN_SYSTEM_SRC,        /* entry kept, src ignored: a system font's file cannot be replaced */
    FONT_WARN_COUNT
} font_warn_t;

/* `name`: the entry or face concerned (NUL-terminated, may be invalid). */
typedef void (*font_warn_fn)(void *ctx, font_warn_t code, const char *name);

const char *font_logic_warn_str(font_warn_t code);

/* The system faces (FONT_SYSTEM_NAME / _SRC, FONT_SYSTEM_BOLD_NAME / _SRC;
 * no fallback). `faces` has room for FONT_FACE_BUILTIN_COUNT; returns that. */
size_t font_logic_builtins(font_face_spec_t *faces);

/* Strings fonts.c must store for an accepted entry (pool bytes, NULs
 * included): a new face's name and src; nothing for a system name. */
size_t font_logic_string_cost(const font_def_t *def, bool overrides_builtin);

/* Apply config entries in order to `faces` (holding *count faces, the
 * system faces first, room for `max`). An entry with a system name
 * configures that face (once): id, fallback and lineheight apply; a
 * non-empty src is warned (FONT_WARN_SYSTEM_SRC) and ignored, the entry is
 * kept. Any other name adds a face (src required). Invalid or
 * duplicate entries, and entries beyond `max` faces or `string_room` pool
 * bytes (font_logic_string_cost), are skipped with a warning (`warn` may
 * be NULL). Fallbacks are NOT resolved here. Returns the entries accepted;
 * `accepted_face` (may be NULL, `n` entries) gets each entry's face index
 * or FONT_FACE_NONE. */
size_t font_logic_merge(font_face_spec_t *faces, size_t *count, size_t max, const font_def_t *defs, size_t n,
                        size_t string_room, int8_t *accepted_face, font_warn_fn warn, void *ctx);

/* Face by name / by id (id >= 1) in `faces`; FONT_FACE_NONE when unknown. */
int font_logic_find(const font_face_spec_t *faces, size_t count, const char *name, size_t len);
int font_logic_find_id(const font_face_spec_t *faces, size_t count, int32_t id);

/* Resolve every face's fallback reference into .fallback: DEFAULT and
 * NONE -> none, NAME, ID; an unknown reference
 * warns and gives none. Then walks each face's chain in face order and
 * cuts, with a warning, the link that closes a cycle (self included) or
 * that would make the chain longer than FONT_CHAIN_MAX faces. Afterwards
 * every chain is acyclic and at most FONT_CHAIN_MAX faces long. */
void font_logic_resolve(font_face_spec_t *faces, size_t count, font_warn_fn warn, void *ctx);

/* ---- sizes ----------------------------------------------------------------- */

/* A size step -> px: 1..5 = the steps 14/24/32/48/72 (text markup
 * \s1..\s5); FONT_PX_MIN..FONT_PX_MAX = px; anything else = fallback_px.
 * (With step 2 = 18 on a title: style_logic_item_px.) */
int font_logic_px(int fontsize, int fallback_px);

/* ---- instance budget ------------------------------------------------------- */

/* LVGL pool bytes that must stay free after a new instance: the objects of
 * a display value re-rendered with the most spans (UI_TEXT_MAX_SPANS labels,
 * ~3 KB) plus margin. Glyph rendering takes nothing from the pool (its
 * scratch is on the system heap: font_logic_glyph_fits). */
#define FONT_POOL_RESERVE 6144u
#define FONT_INSTANCE_POOL 512u   /* one tiny_ttf instance: ~430 B (descriptor + 3 cache headers) */
#define FONT_POOL_BIGGEST 1024u   /* largest free block needed */

typedef enum { FONT_BUDGET_OK = 0, FONT_BUDGET_TABLE_FULL, FONT_BUDGET_POOL_LOW } font_budget_t;

/* May one more tiny_ttf instance be made? `used`/`max`: instance table
 * slots (used = 0 when the slot already exists, as in a reload);
 * `pool_free`/`pool_biggest`: lv_mem_monitor's free_size and
 * free_biggest_size. */
font_budget_t font_logic_budget(size_t used, size_t max, size_t pool_free, size_t pool_biggest);

/* ---- glyph scratch ------------------------------------------------------- */

/* stb_truetype rasterizes a glyph with temporary allocations (measured: up
 * to ~5 KB at 14 px, ~12 KB at 72 px), freed before the glyph is returned.
 * They come from the system heap (fonts.c, fb_tiny_ttf_malloc), which every
 * task shares, so a glyph is rendered only when that leaves
 * FONT_HEAP_RESERVE for the rest of the firmware (TLS, HTTP, MQTT). */
#define FONT_HEAP_RESERVE 16384u
#define FONT_GLYPH_BIGGEST 4096u /* largest single scratch block needed */

/* Worst-case scratch for a glyph of box_w x box_h px. */
size_t font_logic_glyph_scratch(uint32_t box_w, uint32_t box_h);

/* May a glyph of box_w x box_h px be rasterized now? `heap_free` /
 * `heap_biggest`: internal heap free bytes and largest free block. */
bool font_logic_glyph_fits(uint32_t box_w, uint32_t box_h, size_t heap_free, size_t heap_biggest);

/* ---- line box -------------------------------------------------------------- */

/* Adjust an instance's line box (in: tiny_ttf's line_height and base_line,
 * base_line = baseline above the line bottom).
 * - lineheight_x100 != 0: line height = px * lineheight / 100, the extra
 *   (or missing) space split evenly above and below (CSS half-leading, as
 *   the IDE preview draws it); base_line stays within 0..line height.
 * - else `legacy_medium` (FONT_FACE_SYSTEM) at px >= 48: the line box of
 *   the old 48/72 px .bin fonts, 1.03 em with the baseline 0.155 em above
 *   the bottom, which page layouts were made with.
 * - else unchanged (the font's hhea metrics). */
void font_logic_line_box(int px, uint16_t lineheight_x100, bool legacy_medium, int32_t *line_height,
                         int32_t *base_line);

#ifdef __cplusplus
}
#endif

#endif
