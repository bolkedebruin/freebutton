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

#ifdef __cplusplus
}
#endif

#endif
