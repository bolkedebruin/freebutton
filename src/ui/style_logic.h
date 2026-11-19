// config.json "styles": the looks of buttons, display items and lists
// (docs/specs/named-styles.md). Plain C, no LVGL, no allocation: the native
// tests build it (test/test_style_logic); the web IDE implements the same
// rules (web/src/js/style-config.ts, keep in sync).
//
//   "styles": {
//     "button":      {"font-size": 32, "title": {"font-size": 18}},   reserved:
//     "displayitem": {"font-size": 14},                               the look of
//     "list":        {"selected": {"background-color": "#ffffff"}},   a kind
//     "big":   {"font-size": 48},
//     "alarm": {"color": "#ff0000", "title": {"color": "#ff8080"}}
//   },
//   "displayitems": [{"name": "co2", "style": "big alarm"}]
//
// A style is a block of properties for the item's main text, with optional
// sub-blocks ("parts") of the same properties:
//
//   part        button         display item        list
//   main        the label      the value           the rows
//   "title"     the top label  the caption         -
//   "unit"      -              the unit            -
//   "selected"  -              -                   the selected row
//   "icon"      the icon       the icon            -      ("color" only)
//
// The lookup of one property of one part of an item:
//   1. the item's styles ("style": up to four names), the last name first:
//      that part's block;
//   2. the style of the item's kind ("button", "displayitem", "list"): that
//      part's block;
//   3. the built-in value (style_logic_defaults).
// 2 and 3 are resolved once, at config load, into one set of properties per
// kind and part: the seven "elements" below. Parts do not inherit from each
// other. Three exceptions:
//   - the unit has no font or size of its own: where neither the item's
//     styles nor "displayitem" set one in "unit", it has the value's;
//   - the selected row has no font of its own either: it has the rows'.
//     It also has the rows' opacity, letter-spacing and text-decoration
//     where no "selected" block sets them (style_logic_selected_row);
//   - the icon's colour, where no "icon" block sets one, is the main colour.
// The font face and the font size are looked up separately, so "big bold"
// gives the size of one style and the face of the other.
#ifndef STYLE_LOGIC_H
#define STYLE_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A kind and part that exists: its properties after steps 2 and 3. */
typedef enum {
    STYLE_EL_KEY_LABEL = 0,   /* button, main */
    STYLE_EL_KEY_TOPLABEL,    /* button, title */
    STYLE_EL_DISPLAY_VALUE,   /* displayitem, main */
    STYLE_EL_DISPLAY_UNIT,    /* displayitem, unit */
    STYLE_EL_DISPLAY_CAPTION, /* displayitem, title */
    STYLE_EL_LIST,            /* list, main */
    STYLE_EL_LIST_SELECTED,   /* list, selected: the roller's LV_PART_SELECTED */
    STYLE_EL_COUNT
} style_element_t;

typedef enum { STYLE_KIND_BUTTON = 0, STYLE_KIND_DISPLAYITEM, STYLE_KIND_LIST, STYLE_KIND_COUNT } style_kind_t;

typedef enum {
    STYLE_PART_MAIN = 0,
    STYLE_PART_TITLE,
    STYLE_PART_UNIT,
    STYLE_PART_SELECTED,
    STYLE_PART_COUNT,                /* parts that hold properties */
    STYLE_PART_ICON = STYLE_PART_COUNT /* holds one colour, see style_logic_icon_* */
} style_part_t;

typedef enum { STYLE_ALIGN_LEFT = 1, STYLE_ALIGN_CENTER, STYLE_ALIGN_RIGHT } style_align_t;
typedef enum { STYLE_DECOR_NONE = 0, STYLE_DECOR_UNDERLINE = 1, STYLE_DECOR_LINE_THROUGH = 2 } style_decor_t;

/* style_props_t.has: which properties a block sets */
#define STYLE_HAS_FONT (1u << 0)         /* font-family     -> face */
#define STYLE_HAS_SIZE (1u << 1)         /* font-size       -> px */
#define STYLE_HAS_COLOR (1u << 2)        /* color */
#define STYLE_HAS_OPA (1u << 3)          /* opacity */
#define STYLE_HAS_ALIGN (1u << 4)        /* text-align */
#define STYLE_HAS_LETTER_SPACE (1u << 5) /* letter-spacing */
#define STYLE_HAS_LINE_HEIGHT (1u << 6)  /* line-height */
#define STYLE_HAS_DECOR (1u << 7)        /* text-decoration */
#define STYLE_HAS_BG (1u << 8)           /* background-color (colour and opacity) */

/* 20 bytes: one per part of a style, in the table of ui_style.c. */
typedef struct {
    uint32_t color;       /* 0xRRGGBB */
    uint32_t bg;          /* 0xAARRGGBB: the colour, and its opacity in the top byte */
    uint16_t has;
    int16_t face;         /* fonts.h face index (FONT_FACE_SYSTEM, ...) */
    int16_t letter_space; /* px, -100..100 */
    uint16_t line_height; /* x100: 125 = 1.25 times the font's line height */
    uint8_t px;           /* FONT_PX_MIN..FONT_PX_MAX */
    uint8_t opa;          /* 0..255 */
    uint8_t align;        /* style_align_t */
    uint8_t decor;        /* style_decor_t bits */
} style_props_t;

/* The colour of an "icon" block: STYLE_ICON_SET | 0xRRGGBB, or 0 when no
 * block sets one. */
#define STYLE_ICON_SET 0x80000000u

/* What config.json sets for the three reserved styles; all-zero = nothing. */
typedef struct {
    style_props_t element[STYLE_EL_COUNT];
    uint32_t icon[STYLE_KIND_COUNT]; /* "button.icon.color", "displayitem.icon.color" */
} style_sheet_t;

/* "button" / "displayitem" / "list" (`len` bytes) -> style_kind_t, -1 for
 * any other name. An item may not name one of these in its "style". */
int style_logic_kind(const char *name, size_t len);

/* The key of a sub-block: "title" / "unit" / "selected" / "icon" ->
 * style_part_t; -1 for anything else (a property name). */
int style_logic_part(const char *name);

/* The element of a kind's part, -1 when the kind has no such part (its
 * block is then ignored). STYLE_PART_ICON has none. */
int style_logic_element(style_kind_t kind, style_part_t part);

/* Step 3, the look without "styles":
 *   button main       system 32 px, #ffffff          (centred)
 *   button title      system bold 18 px, #ffd700     (centred)
 *   displayitem main  system 14 px, #ffffff
 *   displayitem unit  #999999 (font and size: the value's)
 *   displayitem title system bold 14 px, #ffd700
 *   list main         system 32 px, #808080, line-height 1.25
 *   list selected     #000000 on #ffd700 (font: the rows')
 * "system" = FONT_FACE_SYSTEM, "system bold" = FONT_FACE_SYSTEM_BOLD. The
 * centring of the button labels is not a property here: it is what a part
 * without text-align does (ui_style.c). */
void style_logic_defaults(style_props_t out[STYLE_EL_COUNT]);

/* `dst` takes every property `src` sets. */
void style_logic_merge(style_props_t *dst, const style_props_t *src);

/* Steps 2 and 3 for every element: the defaults, then what the kind's
 * style sets. `sheet` NULL = no reserved style in the config. */
void style_logic_resolve(const style_sheet_t *sheet, style_props_t out[STYLE_EL_COUNT]);

/* The size of a "\s" step in a text (text_spans.h): 1..5 = 14/24/32/48/72,
 * except step 2 of a title (top label, caption) = 18; FONT_PX_MIN..
 * FONT_PX_MAX = px; anything else = `fallback_px`. */
int style_logic_item_px(int fontsize, bool title, int fallback_px);

/* ---- properties (the JSON walk is the caller's) -------------------------- */

typedef enum {
    STYLE_PROP_OK = 0,
    STYLE_PROP_UNKNOWN,      /* not a property name */
    STYLE_PROP_BAD_VALUE,    /* wrong type or out of range */
    STYLE_PROP_UNKNOWN_FONT, /* font-family names no font */
    /* a script changes a style (style_logic_script_*, ui_style.h) only: */
    STYLE_PROP_NO_PART,      /* the style has no such block (a kind: no such part) */
    STYLE_PROP_FIXED,        /* font-family / font-size / line-height: not changed at run time */
    STYLE_PROP_NO_MEMORY,    /* ui_style.h: no heap for the copy of the config values, or no room in
                                the LVGL pool for one more property */
} style_prop_status_t;

/* Resolves font-family: a font name (`len` bytes, not NUL-terminated) or,
 * when `is_id`, a config font id. Returns a face index >= 0 or -1.
 * (Same shape as text_spans.h's text_font_resolver_t.) */
typedef int (*style_font_resolver_t)(const char *name, size_t len, bool is_id, int32_t id, void *ctx);

/* How the caller passes a JSON number for `prop` to style_logic_set_number:
 *   1    an integer, as it is (font-size, letter-spacing, a font-family id)
 *   100  a number of at most two decimals, times 100 (opacity 0..1 ->
 *        0..100, line-height 0.8..3.0 -> 80..300); this file has no
 *        floating point
 *   0    the property takes no number, or is not a property. */
int style_logic_prop_scale(const char *prop);

/* Set one property from a JSON string: font-family (a font name), color
 * ("#rrggbb"), text-align ("left" / "center" / "right"), text-decoration
 * ("none" / "underline" / "line-through" / both words in either order),
 * background-color ("#rrggbb" = opaque, or "#rrggbbaa"). On an error `p` is
 * unchanged. */
style_prop_status_t style_logic_set_string(style_props_t *p, const char *prop, const char *value,
                                           style_font_resolver_t resolve, void *ctx);

/* Set one property from a JSON number, scaled as style_logic_prop_scale
 * says: font-family (a config font id >= 1), font-size (FONT_PX_MIN..
 * FONT_PX_MAX), letter-spacing (-100..100), opacity (0..100 -> stored
 * 0..255), line-height (80..300). */
style_prop_status_t style_logic_set_number(style_props_t *p, const char *prop, int32_t value,
                                           style_font_resolver_t resolve, void *ctx);

/* An "icon" block: "color" ("#rrggbb") is its one property. */
style_prop_status_t style_logic_icon_set_string(uint32_t *icon, const char *prop, const char *value);

/* "#rrggbb" -> 0xRRGGBB; false when malformed. */
bool style_logic_parse_color(const char *s, uint32_t *out);

/* "#rrggbb" -> 0xFFRRGGBB, "#rrggbbaa" -> 0xAARRGGBB; false when malformed. */
bool style_logic_parse_background(const char *s, uint32_t *out);

/* ---- names ----------------------------------------------------------------- */

#define STYLE_MAX 32  /* styles a config may define, the three reserved ones included */
#define STYLE_NAMED_MAX (STYLE_MAX - STYLE_KIND_COUNT) /* the others: the table of ui_style.c */
#define STYLE_NAME_MAX 24 /* docs/NAMES.md: [A-Za-z_][A-Za-z0-9_]{0,23} */
#define STYLE_ITEM_MAX 4  /* styles an item may list */

/* A style name: 1..STYLE_NAME_MAX of [A-Za-z0-9_], not starting with a
 * digit, case sensitive (`len` bytes, not NUL-terminated). */
bool style_logic_name_valid(const char *name, size_t len);

typedef enum {
    STYLE_NAMED_OK = 0,
    STYLE_NAMED_BAD_NAME, /* skipped: not a style name */
    STYLE_NAMED_FULL,     /* skipped: STYLE_NAMED_MAX styles are defined already */
} style_named_status_t;

/* May "styles" define `name` (not a reserved name) as its next style, after
 * `count` accepted ones? */
style_named_status_t style_logic_named_check(const char *name, size_t len, size_t count);

/* The names of the styles, each stored once, in one block the caller owns:
 * per name one length byte and its bytes (1 + len; no terminator), in
 * definition order. A style is known by its index in that order.
 *  add:  appends `name` at `used` bytes; returns the bytes used after it.
 *        The caller has checked the name and the room (1 + len).
 *  find: index of `name` among the `count` names, -1 when unknown.
 *  get:  the name at `index` (*len set), NULL when index >= count. */
size_t style_logic_names_add(char *block, size_t used, const char *name, size_t len);
int style_logic_names_find(const char *block, size_t count, const char *name, size_t len);
const char *style_logic_names_get(const char *block, size_t count, size_t index, size_t *len);

/* A style that could not be made at boot (no room in the LVGL pool for its
 * properties) is dropped: bit i of `dropped` = the style at index i. Such a
 * style is unknown everywhere (config items, scripts), as if "styles" did
 * not define it; the indexes of the others do not move.
 *  ref:    the ref (index + 1) of `name` among the `count` names, 0 when
 *          unknown or dropped.
 *  usable: how many of the `count` styles are not dropped. */
uint8_t style_logic_named_ref(const char *block, size_t count, uint32_t dropped, const char *name, size_t len);
size_t style_logic_named_usable(size_t count, uint32_t dropped);

/* ---- the style list of an item ("style": "big alarm") ---------------------- */

/* Up to STYLE_ITEM_MAX refs in one word, so that an item's list changes as
 * a whole: byte i (bits 8i..8i+7) is the ref of the i-th name, 0 = no name
 * there; the names fill from byte 0 without gaps. 0 = no style. A later
 * name wins where two set the same property. */
typedef uint32_t style_list_t;

uint8_t style_logic_list_ref(style_list_t list, size_t i);
size_t style_logic_list_count(style_list_t list);

/* The next name of a "style" string: names are separated by one or more
 * spaces. *cursor moves past the name; false at the end of the string. */
bool style_logic_list_next(const char **cursor, const char **name, size_t *len);

/* `list` with `ref` as its last name. A ref the list already has moves to
 * the end (a name given twice counts once, at its last place). false, and
 * *list unchanged, when that would be a fifth name. */
bool style_logic_list_add(style_list_t *list, uint8_t ref);

/* `list` without `ref`; the names after it move up. Nothing changes when
 * the list does not have it. */
void style_logic_list_remove(style_list_t *list, uint8_t ref);

/* ---- the lookup ------------------------------------------------------------ */

/* One part of an item: `kind` is the element (steps 2 and 3), `layers` the
 * blocks of that part in the item's styles, in list order (NULL where a
 * style has no such block). `out` gets every property, each from the last
 * layer that sets it, else from `kind`; out->has says which are set at
 * all. */
void style_logic_lookup(const style_props_t *kind, const style_props_t *const *layers, size_t n, style_props_t *out);

/* Does one of the item's own styles (not the kind) set a property of
 * `has_bits` in this part? */
bool style_logic_layers_set(const style_props_t *const *layers, size_t n, uint16_t has_bits);

/* A part whose font follows another's (the unit: the value's; the selected
 * row: the rows'): the face and the px that `part` (a lookup result) sets,
 * each separately, else `base_face` / `base_px`. */
void style_logic_follow_font(const style_props_t *part, int base_face, int base_px, int *face, int *px);

/* The font the shared LVGL styles of a part give without a local font: a
 * style holds a font instance only when its block sets both font-family
 * and font-size, and the style added last wins. So: the last layer with
 * both, else `kind` when it has both; false when none holds a font. The
 * component compares this with the lookup's face and px, and sets a local
 * font where they differ. */
bool style_logic_shared_font(const style_props_t *kind, const style_props_t *const *layers, size_t n, int *face,
                             int *px);

/* The properties of a block that go into its shared lv_style_t when an
 * item's style is added to an object of `part`:
 *   color, opacity, letter-spacing, text-decoration: always;
 *   text-align: main and title (ignored in unit and selected);
 *   font-family + font-size: together, as one font instance;
 *   background-color: selected only. In a main block it is the whole
 *     roller's background, which the list sets itself (List.cpp), so that
 *     it has no effect on a button or a display item;
 *   line-height: never. The gap depends on the font; the component sets it
 *     per object.
 * 0: the block holds nothing for LVGL and its style is not added. */
uint16_t style_logic_lvgl_has(const style_props_t *p, style_part_t part);

/* Of those, what a span label needs from the style itself (its font and
 * colour are local already): opacity, letter-spacing, text-decoration and
 * text-align. 0: the label does not get the style. */
uint16_t style_logic_span_has(const style_props_t *p, style_part_t part);

/* The alignment of a part's text in its item: the text-align of a lookup
 * result (style_align_t), 0 when no style sets one. A "\a" in the text
 * wins over it; with neither, a display item part is as wide as its text,
 * a button label is centred and a list is left aligned. */
uint8_t style_logic_text_align(const style_props_t *resolved);

/* The selected row of a list as it is drawn. `selected` and `rows` are the
 * lookup results of the two parts. LVGL takes an inheritable property that
 * the roller's selected part does not hold from the roller itself, so the
 * selected row has the rows' opacity, letter-spacing and text-decoration
 * where no "selected" block (of the list's styles or of "list") sets them.
 * `out` is `selected` with those three filled in (their has bits too).
 * The font follows the rows' in the same way (style_logic_follow_font); the
 * colour and the background never do: the built-in selected row has both. */
void style_logic_selected_row(const style_props_t *selected, const style_props_t *rows, style_props_t *out);

/* LVGL's text_line_space for a font whose line is `font_line_height` px
 * high: (line-height - 1) lines. 125 gives line_height / 4, the gap a list
 * always had. */
int style_logic_line_space(int font_line_height, int line_height_x100);

/* The tint of an icon: the "icon" colour of the last of the item's styles
 * that has one (`layers`: their icon words, in list order), else the
 * kind's; false when none sets one (the tint is then the main text
 * colour). */
bool style_logic_icon_color(uint32_t kind_icon, const uint32_t *layers, size_t n, uint32_t *rgb);

/* ---- a script changes a value (fb.style.set) ------------------------------- */

/* "color" -> main; "title.color", "unit.color", "selected.color",
 * "icon.color" -> that part. Returns the property name after the prefix
 * and sets *part; a prefix that is no part gives the whole string as a
 * main property (which is then unknown). */
const char *style_logic_split_prop(const char *dotted, style_part_t *part);

/* May a script set `prop` in a block? STYLE_PROP_UNKNOWN: no property
 * name. STYLE_PROP_FIXED: font-family, font-size or line-height (they are
 * computed per item and per font, and a new size makes a font instance
 * that is never freed). STYLE_PROP_OK: every other property, whether the
 * block sets it in config or not. */
style_prop_status_t style_logic_script_settable(const char *prop);

/* A number a script gave that is no number for a style (a fraction for an
 * integer property, more than two decimals, NaN): out of every range, so
 * style_logic_script_set_number reports the property first (unknown,
 * fixed) and only then STYLE_PROP_BAD_VALUE. */
#define STYLE_NUMBER_INVALID INT32_MIN

/* One value of a block that exists. The checks, in this order:
 *   1. the name is a property             else STYLE_PROP_UNKNOWN
 *   2. it is not one of the fixed three   else STYLE_PROP_FIXED
 *   3. the value is valid, as in config   else STYLE_PROP_BAD_VALUE
 * A property the block did not set is added (its has bit is set). On an
 * error `p` is unchanged. A block the style does not have is the caller's
 * (ui_style.c): STYLE_PROP_NO_PART, between 2 and 3. */
style_prop_status_t style_logic_script_set_string(style_props_t *p, const char *prop, const char *value);
style_prop_status_t style_logic_script_set_number(style_props_t *p, const char *prop, int32_t value);

/* The same for an icon word: only "color"; set or not before. */
style_prop_status_t style_logic_script_icon_set_string(uint32_t *icon, const char *prop, const char *value);

/* ---- which styles a script changed ------------------------------------------ */

/* One word with a bit per style: bit i = the style of ref i + 1 (the
 * table, STYLE_NAMED_MAX at most), and above those one bit per kind for
 * "button", "displayitem" and "list". */
#define STYLE_CHANGED_KIND(kind) (1u << (STYLE_NAMED_MAX + (unsigned)(kind)))

/* Does an item of `kind` whose styles are `list` use a style of `changed`?
 * Its kind's style always, and the styles of its list. */
bool style_logic_changed(uint32_t changed, style_kind_t kind, style_list_t list);

/* ---- keys the config no longer has ----------------------------------------- */

typedef enum {
    CONFIG_SCOPE_TOP = 0,     /* a key of the config itself */
    CONFIG_SCOPE_BUTTON,      /* a key of a "buttons" entry */
    CONFIG_SCOPE_DISPLAYITEM, /* a key of a "displayitems" entry */
    CONFIG_SCOPE_LIST,        /* a key of a "lists" entry */
    CONFIG_SCOPE_BROKER,      /* a key of a "brokers" entry */
    CONFIG_SCOPE_COUNT
} config_scope_t;

#define CONFIG_REMOVED_KEYS 24 /* at most this many different key names (one bit each for "warned") */

/* Is `key` (`len` bytes) in `scope` one the firmware read before and no
 * longer reads? Returns its number (0..CONFIG_REMOVED_KEYS-1; the same for
 * one key name in every scope, so that it is reported once), -1 when the
 * key is not a removed one. *hint: what to use instead, for the warning. */
int style_logic_removed_key(config_scope_t scope, const char *key, size_t len, const char **hint);

#ifdef __cplusplus
}
#endif

#endif
