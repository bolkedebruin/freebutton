// config.json "style": the text styles of the UI elements, cascading like
// LVGL styles (docs/STYLE.md). Plain C, no LVGL: the native tests build it
// (test/test_style_logic); the web IDE implements the same rules
// (web/src/js/style-config.ts, keep in sync).
//
//   "style": {
//     "fonts": [ ... ],                                   (fonts.h, font_def_t)
//     "text":  {"text_font": "montserrat", "text_color": "#ffffff"},       role
//     "title": {"text_font": "montserrat_semibold", "text_color": "#ffd700"},
//     "key":     {"label": {...}, "toplabel": {...}},                      elements
//     "display": {"value": {...}, "unit": {...}, "caption": {...}},
//     "list":    {..., "selected": {...}}
//   }
//
// Layers, lowest first (LVGL: theme < added styles < local styles):
//   1. the firmware defaults (style_logic_defaults: the look before "style"
//      existed)
//   2. the element's role ("text": key.label, display.value, display.unit,
//      list; "title": key.toplabel, display.caption)
//   3. the element ("key.label", ...)
//   4. the item (config fields "font"/"fontsize"/"topfont"/..., list
//      "color"/"focuscolor"/"focustextcolor", JS setFont, \f \s \c markup):
//      local styles, applied by the components, not here.
// A property a layer does not set is taken from the layer below.
// Exceptions, as LVGL's inheritance has them:
//   - display.unit has no font or size of its own by default: it takes the
//     value's (after the item layer) unless "style" sets them for the unit.
//     Its text_font and text_size come only from the "display.unit" element
//     (layer 3): neither the defaults nor the role "text" give it one (a
//     role font reaches the unit through the value). Its other properties
//     cascade as for every element.
//   - list.selected (LV_PART_SELECTED) starts from the resolved list (an
//     inheritable property of a non-MAIN part falls back to MAIN), then its
//     own properties (its defaults, then "list.selected"), which win: the
//     selected row keeps its black text when "list" sets a text_color.
//   - bg_color / bg_opa exist on list.selected only.
//
// Property names are LVGL's style property names. "text_size" (px) is the
// one extension: LVGL's text_font is a font at one size, so text_font +
// text_size resolve together into one font instance (fonts_get(face, px)).
#ifndef STYLE_LOGIC_H
#define STYLE_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STYLE_EL_KEY_LABEL = 0,
    STYLE_EL_KEY_TOPLABEL,
    STYLE_EL_DISPLAY_VALUE,
    STYLE_EL_DISPLAY_UNIT,
    STYLE_EL_DISPLAY_CAPTION,
    STYLE_EL_LIST,
    STYLE_EL_LIST_SELECTED, /* the roller's LV_PART_SELECTED */
    STYLE_EL_COUNT
} style_element_t;

typedef enum { STYLE_ROLE_TEXT = 0, STYLE_ROLE_TITLE, STYLE_ROLE_COUNT } style_role_t;

typedef enum { STYLE_ALIGN_LEFT = 1, STYLE_ALIGN_CENTER, STYLE_ALIGN_RIGHT } style_align_t;
typedef enum { STYLE_DECOR_NONE = 0, STYLE_DECOR_UNDERLINE = 1, STYLE_DECOR_STRIKETHROUGH = 2 } style_decor_t;

/* style_props_t.has: which properties a layer sets */
#define STYLE_HAS_FONT (1u << 0)         /* text_font  -> face */
#define STYLE_HAS_SIZE (1u << 1)         /* text_size  -> px */
#define STYLE_HAS_COLOR (1u << 2)        /* text_color */
#define STYLE_HAS_OPA (1u << 3)          /* text_opa */
#define STYLE_HAS_ALIGN (1u << 4)        /* text_align */
#define STYLE_HAS_LETTER_SPACE (1u << 5) /* text_letter_space */
#define STYLE_HAS_LINE_SPACE (1u << 6)   /* text_line_space */
#define STYLE_HAS_DECOR (1u << 7)        /* text_decor */
#define STYLE_HAS_BG_COLOR (1u << 8)     /* bg_color (list.selected only) */
#define STYLE_HAS_BG_OPA (1u << 9)       /* bg_opa (list.selected only) */

typedef struct {
    uint16_t has;
    int16_t face;         /* fonts.h face index (FONT_FACE_SYSTEM, ...) */
    uint8_t px;           /* FONT_PX_MIN..FONT_PX_MAX */
    uint8_t opa;          /* 0..255 */
    uint8_t align;        /* style_align_t */
    uint8_t decor;        /* style_decor_t bits */
    int16_t letter_space; /* px, -100..100 */
    int16_t line_space;   /* px, -100..100 */
    uint8_t bg_opa;       /* 0..255 */
    uint32_t color;       /* 0xRRGGBB */
    uint32_t bg_color;    /* 0xRRGGBB */
} style_props_t;

/* What config.json "style" sets (layers 2 and 3); all-zero = nothing. */
typedef struct {
    style_props_t role[STYLE_ROLE_COUNT];
    style_props_t element[STYLE_EL_COUNT];
} style_sheet_t;

/* The role of an element (STYLE_EL_LIST_SELECTED: STYLE_ROLE_TEXT, via
 * the list). */
style_role_t style_logic_role_of(style_element_t el);

/* Layer 1: the firmware defaults, i.e. the look before "style" existed:
 *   key.label        system 32 px, #ffffff
 *   key.toplabel     system bold 18 px, #ffd700
 *   display.value    system 14 px, #ffffff
 *   display.unit     #999999 (font and size: the value's)
 *   display.caption  system bold 14 px, #ffd700
 *   list             system 32 px, #808080
 *   list.selected    bg #ffd700 (opa 255), text #000000
 * "system" = FONT_FACE_SYSTEM, "system bold" = FONT_FACE_SYSTEM_BOLD. */
void style_logic_defaults(style_props_t out[STYLE_EL_COUNT]);

/* `dst` takes every property `src` sets. */
void style_logic_merge(style_props_t *dst, const style_props_t *src);

/* Layers 1-3 for every element: defaults <- role <- element (the unit:
 * font and size from its element only, see above); list.selected = the
 * resolved list (bg excepted) <- (list.selected defaults <- element).
 * `sheet` NULL = no "style": the defaults, with the list.selected rule. */
void style_logic_resolve(const style_sheet_t *sheet, style_props_t out[STYLE_EL_COUNT]);

/* Item layer size: config / script "fontsize" -> px. 1..5 = the legacy
 * steps 14/24/32/48/72, except step 2 of a title element (key.toplabel,
 * display.caption) = 18 (as before "style"); FONT_PX_MIN..FONT_PX_MAX =
 * px; 0 or anything else = `fallback_px`. */
int style_logic_item_px(int fontsize, bool title, int fallback_px);

/* ---- parsing (the JSON walk is the caller's: ConfigManager) ----------- */

/* "style" key of an element: group "key"/"display"/"list", name
 * "label"/"toplabel"/"value"/"unit"/"caption"/"selected" (NULL for "list"
 * itself). -1 when unknown. */
int style_logic_element(const char *group, const char *name);

/* "text" / "title" -> role; -1 when unknown. */
int style_logic_role(const char *name);

typedef enum {
    STYLE_PROP_OK = 0,
    STYLE_PROP_UNKNOWN,      /* not a property name (or not allowed here: bg_* outside list.selected) */
    STYLE_PROP_BAD_VALUE,    /* wrong type or out of range */
    STYLE_PROP_UNKNOWN_FONT, /* text_font names no font */
} style_prop_status_t;

/* Resolves text_font: a font name (`len` bytes, not NUL-terminated) or,
 * when `is_id`, a config font id. Returns a face index >= 0 or -1.
 * (Same shape as text_spans.h's text_font_resolver_t.) */
typedef int (*style_font_resolver_t)(const char *name, size_t len, bool is_id, int32_t id, void *ctx);

/* Set one property from a JSON string value: text_font (name),
 * text_color / bg_color ("#rrggbb" or "rrggbb"), text_align
 * ("left"/"center"/"right"), text_decor ("none"/"underline"/
 * "strikethrough"/"underline strikethrough"). `allow_bg`: bg_* allowed
 * (list.selected). On an error `p` is unchanged. */
style_prop_status_t style_logic_set_string(style_props_t *p, const char *prop, const char *value, bool allow_bg,
                                           style_font_resolver_t resolve, void *ctx);

/* Set one property from a JSON integer value: text_font (config font id),
 * text_size (FONT_PX_MIN..FONT_PX_MAX), text_opa / bg_opa (0..255),
 * text_letter_space / text_line_space (-100..100). */
style_prop_status_t style_logic_set_number(style_props_t *p, const char *prop, int32_t value, bool allow_bg,
                                           style_font_resolver_t resolve, void *ctx);

/* "#rrggbb" / "rrggbb" -> 0xRRGGBB; false when malformed. */
bool style_logic_parse_color(const char *s, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif
