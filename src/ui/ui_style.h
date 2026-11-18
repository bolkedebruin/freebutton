// The LVGL side of config.json "style" (docs/STYLE.md): one shared static
// lv_style_t per UI element, filled with the element's resolved properties
// (style_logic.h layers 1-3). The components add these styles to their
// objects; item fields (config "font"/"fontsize"/..., list colours, JS
// setFont, \f \s \c markup) are local styles on top, so LVGL's own
// precedence and inheritance do the rest and no per-object memory is added.
//
//   element                 lv_style_t added to                selector
//   key.label               the key's bottom label             MAIN
//   key.toplabel            the key's top label                MAIN
//   display.value           value container + its span labels  MAIN
//   display.unit            unit container + its span labels   MAIN
//   display.caption         caption container + span labels    MAIN
//   list                    the roller                         MAIN
//   list.selected           the roller                         LV_PART_SELECTED
//
// The styles hold fonts_get() instances: their addresses never change, so a
// font reload needs nothing here.
//
// Threads: ui_style_apply and ui_style_get run with the LVGL lock held.
// ui_style_resolved only reads plain data that changes in ui_style_apply
// (config load, before any item is built): any task.
//
// Named styles (config "style.styles", layer 3a of docs/STYLE.md): one
// shared lv_style_t per named style, added to the objects of a part after
// the element style. LVGL inserts an added style before the other added
// styles (lv_obj_add_style) and takes a property from the first style that
// has it (get_prop_core), so the style added last wins among added styles;
// local styles (the item layer) are before both. A property the named
// style does not set therefore comes from the element style, and an item
// field still wins.
//
// The table is one heap block sized to the config (nothing without named
// styles): per style its properties, its lv_style_t and a flag, then the
// names, each once. An item refers to a style by `ref` = index + 1 (one
// byte, 0 = none).
//
// Threads for the named styles: the table is made at config load. After
// that only fb.style.set changes values (ui_style_named_set_*: any task,
// LVGL lock held, plain data and a dirty bit); the LVGL task fills the
// lv_style_t from them (ui_style_named_refresh). Which properties a style
// has, its text_font and its text_size never change after load: has bits,
// face and px of ui_style_named_props() may be read by any task; the other
// values with the LVGL lock held.
#ifndef UI_STYLE_H
#define UI_STYLE_H

#include "lvgl.h"
#include "style_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fill the shared styles with `resolved` (style_logic_resolve()) and have
 * the objects that use them restyle. Idempotent (each style is initialised
 * once; a property a style no longer has is removed). LVGL lock held. */
void ui_style_apply(const style_props_t resolved[STYLE_EL_COUNT]);

/* The element's shared style. Before ui_style_apply (no config loaded) the
 * styles hold the firmware defaults. LVGL lock held. */
lv_style_t *ui_style_get(style_element_t el);

/* The element's resolved properties (the firmware defaults before
 * ui_style_apply): the base of the components' item layer. Never NULL. */
const style_props_t *ui_style_resolved(style_element_t el);

/* ---- named styles ------------------------------------------------------ */

/* Config load, LVGL lock held, no object uses a named style (the items of
 * the previous config are gone): frees the previous table and makes one
 * for `count` styles (<= STYLE_NAMED_MAX) whose names take `name_bytes`
 * (1 + length each). count 0 allocates nothing. false: no memory (logged
 * by the caller); the device then runs without named styles. */
bool ui_style_named_alloc(size_t count, size_t name_bytes);

/* Config load: the next style of the table, named `name` (checked by the
 * caller, style_logic_named_check). Returns its properties, all unset, for
 * the caller to parse into; NULL when the table is full. */
style_props_t *ui_style_named_add(const char *name, size_t len);

/* Config load, LVGL lock held, after the last ui_style_named_add: fills
 * each style's lv_style_t. Returns the styles that could not be filled for
 * lack of LVGL pool (bit i: the style of ref i + 1; the caller logs them,
 * ui_style_named_name still gives their names). Such a style is dropped:
 * unknown from then on, as if the config did not define it
 * (ui_style_named_find gives 0, ui_style_named_count leaves it out). */
uint16_t ui_style_named_apply(void);

/* Number of usable named styles (0: the config has none, or none could be
 * made): what gates the script methods. Any task, after config load. */
size_t ui_style_named_count(void);

/* Heap bytes of the table (0 without named styles), for the boot log. */
size_t ui_style_named_bytes(void);

/* ref (index + 1) of the style named `name`, 0 when unknown (also a style
 * ui_style_named_apply dropped). Any task, after ui_style_named_apply. */
uint8_t ui_style_named_find(const char *name, size_t len);

/* The name of style `ref` (*len set; not NUL-terminated), NULL for 0 or a
 * ref past the table. Any task. */
const char *ui_style_named_name(uint8_t ref, size_t *len);

/* The properties of style `ref` now, NULL for 0, an unknown ref or a style
 * that could not be filled. See "Threads" above. */
const style_props_t *ui_style_named_props(uint8_t ref);

/* LVGL lock held: have `obj` carry the named style `want` (0 = none) for
 * `selector`, where it carries `*have` now (0 = none; the caller keeps
 * that byte per object and part, 0 for a new object). Call it after the
 * element style was added, so the named style wins over it. Adding grows
 * the object's style list in the LVGL pool, where a failed allocation
 * halts the LVGL task: without room nothing is added (logged once), *have
 * is 0 and false is returned. Nothing here tries again: the caller then
 * (1) computes the part's font cascade from *have, what the object
 * carries, not from `want`, so that font, size and colour stay those of
 * one look, and (2) sets its dirty flag again and asks for a later walk
 * (ui::requestPendingRenderLater), where it calls this again. true: the
 * object carries `want` (or `want` is no style: nothing to wait for). */
bool ui_style_named_attach(lv_obj_t *obj, uint8_t *have, uint8_t want, lv_style_selector_t selector);

/* The lv_style_t of `ref` for an object made just now (a span label):
 * NULL for none. LVGL lock held; the caller has checked the pool. */
const lv_style_t *ui_style_named_get(uint8_t ref);

/* fb.style.set: one value of style `ref` (any task, LVGL lock held). Only
 * plain data changes here; the caller then requests a pending render and
 * the LVGL task does the rest (ui_style_named_refresh). The config values
 * of all named styles are copied to a heap block at the first change
 * (STYLE_PROP_NO_MEMORY, nothing changed, when that fails). Other errors:
 * style_logic_named_set_*. */
style_prop_status_t ui_style_named_set_string(uint8_t ref, const char *prop, const char *value);
style_prop_status_t ui_style_named_set_number(uint8_t ref, const char *prop, int32_t value);

/* Script restart (any task, LVGL lock held): the config values again for
 * every style a script changed; frees the copy. Allocates nothing. The
 * caller requests a pending render. */
void ui_style_named_restore(void);

/* LVGL task, LVGL lock held, around one walk of the pending renders
 * (PendingRender.h setWalkHooks): refresh fills the lv_style_t of every
 * style whose values changed and has LVGL restyle the objects that carry
 * it; until refresh_done, ui_style_named_changed(ref) is true for those
 * styles. Span labels hold a local colour and font made when their text
 * was rendered, which LVGL's restyle does not reach, so a widget that uses
 * a changed style draws its text again in that walk. */
void ui_style_named_refresh(void);
void ui_style_named_refresh_done(void);
bool ui_style_named_changed(uint8_t ref);

#ifdef __cplusplus
}
#endif

#endif
