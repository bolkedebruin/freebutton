//
// Created by Bolke de Bruin on 03/07/2024.
//

#ifndef BUTTON_UI_TEXT_H
#define BUTTON_UI_TEXT_H

#define MAX_TEXT_LENGTH 256

#include <stdint.h>
#include <lvgl.h>

typedef struct {
    uint32_t character;         /* Unicode character */
    lv_color_t color;           /* Font color */
    uint8_t size;               /* Button Font size 1,2,3... */
    lv_text_align_t text_align; /* Text alignment */
    lv_align_t vertical_align;  /* Vertical alignment */
} button_text_t;

void ui_render_text(lv_obj_t * obj, const char * text, lv_style_t * default_style, uint8_t base_font_size, bool bold);
size_t parse_text(const char * text, size_t len, button_text_t * button_text);

#endif //BUTTON_UI_TEXT_H
