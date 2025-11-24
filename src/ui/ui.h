#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// Box creation and manipulation functions
void ui_box_create_styles(void);
void ui_box_set_default_title_color(long color);
lv_obj_t* ui_box_create(lv_obj_t* parent);
void ui_box_update(lv_obj_t* box, const char* caption, const char* text, const char* unit,
                   int x, int y, int font, int width, int align, int boxtype);
void ui_box_set_caption(lv_obj_t* box, const char* caption);
void ui_box_set_text(lv_obj_t* box, const char* text);
void ui_box_set_hint(lv_obj_t* box, const char* hint);
void ui_box_set_font_size(lv_obj_t* box, uint8_t size, int boxtype);

// Alias for draw_box function as requested in the requirements
static inline void draw_box(lv_obj_t* box, const char* label, const char* value, const char* unit,
                           int x, int y, int fontsize, int width, int align, int boxtype) {
    ui_box_update(box, label, value, unit, x, y, fontsize, width, align, boxtype);
}

#ifdef __cplusplus
}
#endif