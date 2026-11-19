// The LVGL side of config.json "styles" (docs/specs/named-styles.md;
// the rules are style_logic.h's).
//
// The kinds. The reserved styles "button", "displayitem" and "list" are
// resolved with the built-in look into seven static elements, one shared
// lv_style_t each, which the components add to their objects first:
//
//   element                 lv_style_t added to                selector
//   button main             the button's bottom label          MAIN
//   button title            the button's top label             MAIN
//   displayitem main        value container + its span labels  MAIN
//   displayitem unit        unit container + its span labels   MAIN
//   displayitem title       caption container + span labels    MAIN
//   list main               the roller                         MAIN
//   list selected           the roller                         LV_PART_SELECTED
//
// The other styles. One heap block sized to the config (nothing when the
// config defines only reserved styles): per style a head (which parts it
// has, its icon colour), per part it has an entry (its properties, one
// shared lv_style_t and a flag), then the names, each once. An item refers
// to a style by `ref` = index + 1 (one byte, 0 = none) and to its styles by
// a style_list_t (four refs in a word).
//
// An item's styles go on its objects after the element style, in list
// order. LVGL inserts an added style before the other added styles
// (lv_obj_add_style) and takes a property from the first style that has it
// (get_prop_core), so the style added last wins among added styles, and a
// property no style of the item sets comes from the element style. Local
// styles are before all of them: the components use those only for what
// has to be computed per object (a font built from two styles, the line
// space, the alignment of a text, a list's background).
//
// The styles hold fonts_get() instances: their addresses never change, so a
// font reload needs nothing here.
//
// Threads: the kinds and the table are made at config load, with the LVGL
// lock held. After that only fb.style.set changes values (ui_style_set_* /
// ui_style_kind_set_*: any task, LVGL lock held, plain data and a dirty
// bit); the LVGL task fills the lv_style_t from them (ui_style_refresh).
// Which properties a block or an element has, its font-family, font-size
// and line-height never change after load: has bits, face, px and
// line_height may be read by any task (ui_style_resolved, ui_style_lookup);
// the other values with the LVGL lock held.
#ifndef UI_STYLE_H
#define UI_STYLE_H

#include "lvgl.h"
#include "style_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the kinds ------------------------------------------------------------- */

/* Fill the seven shared styles with `resolved` (style_logic_resolve()) and
 * have the objects that use them restyle; `icon`: the kinds' icon colours
 * (style_sheet_t.icon; NULL = none). Idempotent (each style is initialised
 * once; a property a style no longer has is removed). `resolved` NULL: the
 * built-in look. LVGL lock held. */
void ui_style_apply(const style_props_t resolved[STYLE_EL_COUNT], const uint32_t icon[STYLE_KIND_COUNT]);

/* The element's shared style. Before ui_style_apply (no config loaded) the
 * styles hold the built-in look. LVGL lock held. */
lv_style_t *ui_style_get(style_element_t el);

/* The element's resolved properties (the built-in look before
 * ui_style_apply). Never NULL. */
const style_props_t *ui_style_resolved(style_element_t el);

/* ---- the table of the other styles ----------------------------------------- */

/* Config load, LVGL lock held, no object uses a style of the table (the
 * items of the previous config are gone): frees the previous table and
 * makes one for `styles` styles (<= STYLE_NAMED_MAX) with `parts` part
 * entries in all, whose names take `name_bytes` (1 + length each). styles 0
 * allocates nothing. false: no memory (logged by the caller); the device
 * then runs with the reserved styles only. */
bool ui_style_table_alloc(size_t styles, size_t parts, size_t name_bytes);

/* Config load: the next style of the table, named `name` (checked by the
 * caller, style_logic_named_check). Returns its ref, 0 when the table is
 * full. */
uint8_t ui_style_table_add(const char *name, size_t len);

/* Config load: the block of `part` (main, title, unit or selected) of the
 * style just added. Returns its properties, all unset, for the caller to
 * parse into; NULL when the table has no entry left or the style has that
 * part already. */
style_props_t *ui_style_table_part(uint8_t ref, style_part_t part);

/* Config load: the icon colour word of a style (0 = not set), for the
 * caller to parse "icon" into (style_logic_icon_set_string). NULL for an
 * unknown ref. */
uint32_t *ui_style_table_icon(uint8_t ref);

/* Config load, LVGL lock held, after the last style was added: fills each
 * part's lv_style_t. Returns the styles that could not be filled for lack
 * of LVGL pool (bit i: the style of ref i + 1; the caller logs them,
 * ui_style_name still gives their names). Such a style is dropped: unknown
 * from then on, as if the config did not define it. */
uint32_t ui_style_table_apply(void);

/* Number of usable styles in the table (the reserved ones are not in it).
 * Any task, after config load. */
size_t ui_style_count(void);

/* Heap bytes of the table (0 without one), for the boot log. */
size_t ui_style_table_bytes(void);

/* ref (index + 1) of the style named `name`, 0 when unknown (also a
 * reserved name, and a style ui_style_table_apply dropped). Any task. */
uint8_t ui_style_find(const char *name, size_t len);

/* The name of style `ref` (*len set; not NUL-terminated), NULL for 0 or a
 * ref past the table. Any task. */
const char *ui_style_name(uint8_t ref, size_t *len);

/* The names of a list, joined by one space, as config.json has them
 * ("big alarm"; "" for no style). `out` holds at least
 * UI_STYLE_LIST_TEXT_MAX bytes. Returns the length. Any task. */
#define UI_STYLE_LIST_TEXT_MAX (STYLE_ITEM_MAX * (STYLE_NAME_MAX + 1))
size_t ui_style_list_text(style_list_t list, char *out, size_t capacity);

/* ---- an item's styles ------------------------------------------------------ */

/* The lookup for one part of an item of `kind` whose styles are `list`:
 * every property from the last style of the list that sets it in that
 * part, else the kind's (style_logic_lookup). *own_size (may be NULL): one
 * of the item's own styles sets font-size there. A part the kind does not
 * have gives all-unset properties. Face, px, line_height and the has bits:
 * any task; the rest with the LVGL lock held. */
void ui_style_lookup(style_kind_t kind, style_part_t part, style_list_t list, style_props_t *out, bool *own_size);

/* The font the shared styles of that part give an object that carries the
 * element style and `list` (style_logic_shared_font); false when none of
 * them holds a font. */
bool ui_style_shared_font(style_kind_t kind, style_part_t part, style_list_t list, int *face, int *px);

/* The icon colour of an item (style_logic_icon_color): false when no style
 * sets one. LVGL lock held. */
bool ui_style_icon_color(style_kind_t kind, style_list_t list, uint32_t *rgb);

/* One part of an item, on the object that draws it: `obj` carries the
 * styles of `*have` for `part` now (the caller keeps that word per object
 * and part; 0 for a new object), under `selector`. */
typedef struct {
    lv_obj_t *obj;
    style_list_t *have;
    style_part_t part;
    lv_style_selector_t selector;
} ui_style_target_t;
#define UI_STYLE_TARGETS_MAX 4 /* the parts of one item */

/* LVGL lock held: have every target carry the styles of `want`. Only the
 * blocks that hold an LVGL property are added (style_logic_lvgl_has), in
 * list order, after the element style the caller added before. Changing an
 * object's style list allocates in the LVGL pool, where a failed allocation
 * halts the LVGL task: the room is checked once, for all the targets
 * together, before the first object is touched. Without it nothing changes
 * (logged once), every *have stays what it was and false is returned. Never
 * half a list, and never one part of an item with the new list and another
 * with the old one. Nothing here tries again: the caller then (1) computes
 * the parts' fonts and colours from *have, what the objects carry, not from
 * `want`, and (2) sets its dirty flag again and asks for a later walk
 * (ui::requestPendingRenderLater). true: every *have is `want`.
 *
 * A caller that uses a style of which ui_style_changed() is true calls this
 * in that walk, also when its list did not change: a script may have given
 * a block its first LVGL property (fb.style.set), or a restart taken it
 * away, and the objects then get or lose that block's style here. Losing
 * one needs no room and always works. Without room to add a new one, the
 * targets carry no style of the list (every *have is 0: the look of the
 * kind, whole) until the caller's retry. */
bool ui_style_attach_all(const ui_style_target_t *targets, size_t count, style_list_t want);

/* The same for one object. */
bool ui_style_attach(lv_obj_t *obj, style_list_t *have, style_list_t want, style_part_t part,
                     lv_style_selector_t selector);

/* A span label made just now for a text of `part`: adds the styles of
 * `list` whose block sets opacity, letter-spacing, text-decoration or
 * text-align (style_logic_span_has), after the element style. A label's
 * font and colour are local, so nothing else of a style reaches it. LVGL
 * lock held; the caller has checked the pool (ui_text.c). */
void ui_style_add_to_span(lv_obj_t *label, style_list_t list, style_part_t part);

/* ---- fb.style.set ---------------------------------------------------------- */

/* One value of a block of style `ref` (any task, LVGL lock held);
 * `part` may be STYLE_PART_ICON ("color" only). Only plain data changes
 * here; the caller then requests a pending render and the LVGL task does
 * the rest (ui_style_refresh). A property the block does not set yet is
 * added; the block itself has to exist (the table is sized at config
 * load): STYLE_PROP_NO_PART for a block the style does not have. Every
 * style has an icon word. The config values of the table and of the kinds
 * are copied to one heap block at the first change; STYLE_PROP_NO_MEMORY,
 * nothing changed, when that fails or when the LVGL pool has no room for
 * one more property. Other errors, and the order of the checks:
 * style_logic_script_set_*. */
style_prop_status_t ui_style_set_string(uint8_t ref, style_part_t part, const char *prop, const char *value);
style_prop_status_t ui_style_set_number(uint8_t ref, style_part_t part, const char *prop, int32_t value);

/* The same for the style of a kind ("button", "displayitem", "list"): one
 * value of the element that `part` is for that kind (ui_style_resolved). A
 * kind has all its parts always; a part it does not have ("unit" of a
 * button, "icon" of a list): STYLE_PROP_NO_PART. */
style_prop_status_t ui_style_kind_set_string(style_kind_t kind, style_part_t part, const char *prop,
                                             const char *value);
style_prop_status_t ui_style_kind_set_number(style_kind_t kind, style_part_t part, const char *prop, int32_t value);

/* Script restart (any task, LVGL lock held): the config values again for
 * every style a script changed, the kinds included (a property a script
 * added is no longer set); frees the copy. Allocates nothing. The caller
 * requests a pending render: the refill of that walk takes the added
 * properties out of the lv_style_t again, and the users of a block that
 * holds nothing for LVGL any more take its style off (ui_style_attach_all). */
void ui_style_restore(void);

/* LVGL task, LVGL lock held, around one walk of the pending renders
 * (PendingRender.h setWalkHooks): refresh fills the lv_style_t of every
 * style whose values changed (for a kind: of its elements) and has LVGL
 * restyle the objects that carry it; until refresh_done,
 * ui_style_changed(kind, list) is true for an item of a kind whose style
 * changed and for a list with a changed style. Span labels hold a local
 * colour and font made when their text was rendered, and an icon its tint,
 * which LVGL's restyle does not reach: a widget that uses a changed style
 * draws its texts again in that walk. */
void ui_style_refresh(void);
void ui_style_refresh_done(void);
bool ui_style_changed(style_kind_t kind, style_list_t list);

#ifdef __cplusplus
}
#endif

#endif
