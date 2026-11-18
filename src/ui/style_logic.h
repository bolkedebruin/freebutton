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
//   - bg_color / bg_opa: the element list.selected has them, and so may a
//     named style, which then only a list can use ("style" / "focusstyle";
//     style_logic_named_fits). No other element has a background.
//
// Named styles ("style.styles": {"alarm": {"text_color": "#ff0000"}, ...})
// are layer 3a, between the element and the item: an item names one per
// part (config "style" / "topstyle" / "labelstyle" / "focusstyle", or a
// script). A named style is flat (properties only). The rules here: the
// names, the font cascade with a named style in it, which element may use a
// style that sets bg_*, and what a script may change at run time.
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
#define STYLE_HAS_BG_COLOR (1u << 8)     /* bg_color (list.selected, and named styles used by a list) */
#define STYLE_HAS_BG_OPA (1u << 9)       /* bg_opa (list.selected, and named styles used by a list) */

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
    STYLE_PROP_UNKNOWN,      /* not a property name (or not allowed here: bg_* outside list.selected
                              * and the named styles) */
    STYLE_PROP_BAD_VALUE,    /* wrong type or out of range */
    STYLE_PROP_UNKNOWN_FONT, /* text_font names no font */
    /* style_logic_named_set_* (a script changes a named style) only: */
    STYLE_PROP_NOT_IN_STYLE, /* the style does not set this property in config */
    STYLE_PROP_FIXED,        /* text_font / text_size: not changed at run time */
    STYLE_PROP_NO_MEMORY,    /* ui_style.h: the config values could not be kept */
} style_prop_status_t;

/* Resolves text_font: a font name (`len` bytes, not NUL-terminated) or,
 * when `is_id`, a config font id. Returns a face index >= 0 or -1.
 * (Same shape as text_spans.h's text_font_resolver_t.) */
typedef int (*style_font_resolver_t)(const char *name, size_t len, bool is_id, int32_t id, void *ctx);

/* Set one property from a JSON string value: text_font (name),
 * text_color / bg_color ("#rrggbb" or "rrggbb"), text_align
 * ("left"/"center"/"right"), text_decor ("none"/"underline"/
 * "strikethrough"/"underline strikethrough"). `allow_bg`: bg_* allowed
 * (list.selected, a named style). On an error `p` is unchanged. */
style_prop_status_t style_logic_set_string(style_props_t *p, const char *prop, const char *value, bool allow_bg,
                                           style_font_resolver_t resolve, void *ctx);

/* Set one property from a JSON integer value: text_font (config font id),
 * text_size (FONT_PX_MIN..FONT_PX_MAX), text_opa / bg_opa (0..255),
 * text_letter_space / text_line_space (-100..100). */
style_prop_status_t style_logic_set_number(style_props_t *p, const char *prop, int32_t value, bool allow_bg,
                                           style_font_resolver_t resolve, void *ctx);

/* "#rrggbb" / "rrggbb" -> 0xRRGGBB; false when malformed. */
bool style_logic_parse_color(const char *s, uint32_t *out);

/* ---- named styles ("style.styles") ------------------------------------- */

#define STYLE_NAMED_MAX 16 /* named styles a config may define */
#define STYLE_NAME_MAX 24  /* docs/NAMES.md: [A-Za-z_][A-Za-z0-9_]{0,23} */

/* A style name: 1..STYLE_NAME_MAX of [A-Za-z0-9_], not starting with a
 * digit, case sensitive (`len` bytes, not NUL-terminated). */
bool style_logic_name_valid(const char *name, size_t len);

typedef enum {
    STYLE_NAMED_OK = 0,
    STYLE_NAMED_BAD_NAME, /* skipped: not a style name */
    STYLE_NAMED_FULL,     /* skipped: STYLE_NAMED_MAX styles are defined already */
} style_named_status_t;

/* May "style.styles" define `name` as its next style, after `count`
 * accepted ones? */
style_named_status_t style_logic_named_check(const char *name, size_t len, size_t count);

/* The names of the named styles, each stored once, in one block the caller
 * owns: per name one length byte and its bytes (1 + len; no terminator),
 * in definition order. A style is known by its index in that order.
 *  add:  appends `name` at `used` bytes; returns the bytes used after it.
 *        The caller has checked the name and the room (1 + len).
 *  find: index of `name` among the `count` names, -1 when unknown.
 *  get:  the name at `index` (*len set), NULL when index >= count. */
size_t style_logic_names_add(char *block, size_t used, const char *name, size_t len);
int style_logic_names_find(const char *block, size_t count, const char *name, size_t len);
const char *style_logic_names_get(const char *block, size_t count, size_t index, size_t *len);

/* A style that could not be made at boot (no room in the LVGL pool for its
 * properties) is dropped: bit i of `dropped` = the style at index i. Such a
 * style is unknown everywhere (config items, scripts), as if "style.styles"
 * did not define it; the indexes of the others do not move.
 *  ref:    the ref (index + 1) of `name` among the `count` names, 0 when
 *          unknown or dropped.
 *  usable: how many of the `count` styles are not dropped. */
uint8_t style_logic_named_ref(const char *block, size_t count, uint16_t dropped, const char *name, size_t len);
size_t style_logic_named_usable(size_t count, uint16_t dropped);

/* May a part of element `el` use the named style `named`? A style that
 * sets bg_color or bg_opa is for a list only (list, list.selected): keys
 * and display items have no background of their own. */
bool style_logic_named_fits(const style_props_t *named, style_element_t el);

/* The font cascade of one part with a named style in it:
 *   face = item field, else named text_font, else the element style's
 *   px   = item fontsize (steps, title rule), else named text_size (px),
 *          else the element style's
 * font_layer gives `out` = `element` with the text_font / text_size that
 * `named` sets (`named` NULL = no named style: `out` = `element`). The
 * item layer then goes over `out` exactly as it went over the element
 * (style_logic_item_px, box_logic.h). For display.unit, where "sets
 * text_font" counts, a font or size of `named` counts as set.
 * Only face, px and their has bits of `out` are meant to be read. */
void style_logic_font_layer(const style_props_t *element, const style_props_t *named, style_props_t *out);

/* Does the part need a local font (LVGL: over every added style)? Yes when
 * the item sets a font or a size (as before named styles), and when the
 * named style sets exactly one of text_font / text_size: the font instance
 * then depends on the layer below. A named style with both holds its font
 * in its own shared style; with neither the element style's applies.
 * item_face FONT_FACE_NONE and item_fontsize 0 = the item sets none. */
bool style_logic_font_local(int item_face, int item_fontsize, const style_props_t *named);

/* Does list.selected keep a font of its own in its shared style? Only when
 * its resolved font differs from the list's, i.e. "style.list.selected"
 * sets a text_font or text_size of its own (`list`, `selected`: the
 * resolved elements). Otherwise its style holds no font and the selected
 * row has the rows' (LVGL: an inheritable property a non-MAIN part does not
 * have comes from MAIN), so an item font on the list reaches it too. */
bool style_logic_selected_own_font(const style_props_t *list, const style_props_t *selected);

/* The font of a list's selected row (LV_PART_SELECTED), lowest first:
 *   1. the rows' effective font (`rows_face`, `rows_px`: the item's "font" /
 *      "fontsize" or a script's setFont, else the rows' named style, else
 *      the list element; what LV_PART_MAIN shows);
 *   2. list.selected's face and px, both, where that element keeps a font
 *      of its own (style_logic_selected_own_font);
 *   3. the text_font and / or text_size of `focus`, the selected row's
 *      named style ("focusstyle"; NULL = none).
 * *face and *px get the result. Returns true when the roller needs it as a
 * local font on LV_PART_SELECTED: `focus` sets exactly one of text_font /
 * text_size, so no shared style holds the instance. (With both, the named
 * style holds it; with neither, list.selected's style or LV_PART_MAIN
 * gives it.) */
bool style_logic_list_selected_font(const style_props_t *list, const style_props_t *selected,
                                    const style_props_t *focus, int rows_face, int rows_px, int *face, int *px);

/* A script changes one value of a named style (fb.style.set): only a
 * property the style sets in config (STYLE_PROP_NOT_IN_STYLE otherwise;
 * adding one would allocate at run time), and not text_font / text_size
 * (STYLE_PROP_FIXED: every user's font cascade would change). The value is
 * checked as in config (style_logic_set_string / _set_number, bg_*
 * allowed). On an error `p` is unchanged. */
style_prop_status_t style_logic_named_set_string(style_props_t *p, const char *prop, const char *value);
style_prop_status_t style_logic_named_set_number(style_props_t *p, const char *prop, int32_t value);

#ifdef __cplusplus
}
#endif

#endif
