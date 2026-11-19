#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "style_logic.h"

// Box creation and manipulation functions
void ui_box_create_styles(void);
lv_obj_t* ui_box_create(lv_obj_t* parent); // NULL when LVGL is out of memory
void ui_box_delete(lv_obj_t* box);  // the box and its descriptor

// A box is given the item's style list (config "style", or a script's: a
// style_list_t of ui_style.h). Caption, value and unit each carry the
// blocks of their part ("title", main, "unit") over the displayitem
// element styles; fonts, alignment and padding follow from the lookup
// through what the parts carry.

// Sets the styles first, then draws caption, text and unit with them.
// These four return false when a text was not drawn for lack of LVGL pool
// (ui_text.h): the caller draws it again later. ui_box_update also when a
// part could not take the styles for the same reason (the part is then
// drawn with what it carried, font, size and padding included): the caller
// calls ui_box_update again later.
bool ui_box_update(lv_obj_t* box, const char* caption, const char* text, const char* unit,
                   int x, int y, style_list_t styles, int width, int align, int boxtype);
bool ui_box_set_caption(lv_obj_t* box, const char* caption);
bool ui_box_set_text(lv_obj_t* box, const char* text);
bool ui_box_set_hint(lv_obj_t* box, const char* hint);
// For the text drawn after it (callers redraw caption, text and unit).
void ui_box_set_styles(lv_obj_t* box, style_list_t styles, int boxtype);

// The font (fonts.h face, px) a part's text has where its markup names
// none: the lookup through the styles the part carries. The unit: its own
// where a "unit" block sets one, else the value's. part: UI_BOX_CAPTION,
// UI_BOX_VALUE or UI_BOX_UNIT. *align (may be NULL): the part's text-align
// (style_align_t, 0 = none; always 0 for the unit).
enum { UI_BOX_CAPTION = 0, UI_BOX_VALUE = 1, UI_BOX_UNIT = 2 };
void ui_box_part_font(lv_obj_t* box, int part, int* face, int* px, uint8_t* align);

// Raster images (icons, ui/MediaImage.h), LVGL lock held.
// Shows dsc on img at the raster's own size (never scaled: LVGL's transform
// path needs a large layer); NULL shows nothing. An A8 raster is tinted with
// tint; other formats are drawn as they are (a recolour would take
// RGB565A8 off LVGL's direct draw path).
void ui_image_set_raster(lv_obj_t* img, const lv_image_dsc_t* dsc, lv_color_t tint);
// The value's icon, left of the value. dsc NULL deletes the icon's image
// object (no LVGL memory while there is no icon). Returns false when the
// image object could not be created (dsc is then not shown).
bool ui_box_set_icon(lv_obj_t* box, const lv_image_dsc_t* dsc, lv_color_t tint);
// Whether the box has an icon image object now.
bool ui_box_has_icon(lv_obj_t* box);
// The icon's tint: the "icon" colour of the styles the value carries, else
// of "displayitem", else the colour of the value text.
lv_color_t ui_box_icon_color(lv_obj_t* box);

// Alias for draw_box function as requested in the requirements
static inline bool draw_box(lv_obj_t* box, const char* label, const char* value, const char* unit,
                           int x, int y, style_list_t styles, int width, int align, int boxtype) {
    return ui_box_update(box, label, value, unit, x, y, styles, width, align, boxtype);
}

#ifdef __cplusplus
}
#endif