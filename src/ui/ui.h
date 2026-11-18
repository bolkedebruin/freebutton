#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// Box creation and manipulation functions
void ui_box_create_styles(void);
lv_obj_t* ui_box_create(lv_obj_t* parent); // NULL when LVGL is out of memory
void ui_box_delete(lv_obj_t* box);  // the box and its descriptor

// The item fonts of a box (the local layer over ui_style.h's display.value /
// display.unit / display.caption styles): a face is a fonts.h face index,
// FONT_FACE_NONE = the style's; a fontsize is config "fontsize" (1..5 =
// legacy step, FONT_PX_MIN..FONT_PX_MAX = px), 0 = the style's.
//  face / fontsize:                 value and unit ("font" / "fontsize")
//  caption_face / caption_fontsize: caption ("labelfont" / "labelfontsize";
//                                   legacy step 2 = 18 px)
// And the named styles below that layer (ui_style.h ref, 0 = none):
//  style:         value and unit ("style")
//  caption_style: caption ("labelstyle")
// (The two refs sit in the padding after the sizes: the struct stays 8 bytes.)
typedef struct {
    int16_t face;
    uint8_t fontsize;
    uint8_t style;
    int16_t caption_face;
    uint8_t caption_fontsize;
    uint8_t caption_style;
} ui_box_fonts_t;

// Sets the fonts first, then draws caption, text and unit with them.
// These four return false when a text was not drawn for lack of LVGL pool
// (ui_text.h): the caller draws it again later. ui_box_update also when a
// part could not take its named style for the same reason (the part is then
// drawn without it, font, size and padding included): the caller calls
// ui_box_update again later.
bool ui_box_update(lv_obj_t* box, const char* caption, const char* text, const char* unit,
                   int x, int y, const ui_box_fonts_t* fonts, int width, int align, int boxtype);
bool ui_box_set_caption(lv_obj_t* box, const char* caption);
bool ui_box_set_text(lv_obj_t* box, const char* text);
bool ui_box_set_hint(lv_obj_t* box, const char* hint);
// For the text drawn after it (callers redraw caption, text and unit).
void ui_box_set_fonts(lv_obj_t* box, const ui_box_fonts_t* fonts, int boxtype);

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
// The colour of the value text (the icon's default tint).
lv_color_t ui_box_text_color(lv_obj_t* box);

// Alias for draw_box function as requested in the requirements
static inline bool draw_box(lv_obj_t* box, const char* label, const char* value, const char* unit,
                           int x, int y, const ui_box_fonts_t* fonts, int width, int align, int boxtype) {
    return ui_box_update(box, label, value, unit, x, y, fonts, width, align, boxtype);
}

#ifdef __cplusplus
}
#endif