//
// Created by Bolke de Bruin on 03/07/2024.
//

#ifndef BUTTON_UI_TEXT_H
#define BUTTON_UI_TEXT_H

#define MAX_TEXT_LENGTH 256
#define MAX_IMAGE_PATH_LENGTH 64

#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>

typedef struct {
    uint32_t character;         /* Unicode character, 0 if this is an image */
    lv_color_t color;           /* Font color */
    uint8_t size;               /* Button Font size 1,2,3... */
    lv_text_align_t text_align; /* Text alignment */
    lv_align_t vertical_align;  /* Vertical alignment */
    char image_path[MAX_IMAGE_PATH_LENGTH]; /* Image path if character==0, empty otherwise */
} button_text_t;

/* Check if this text element is an image reference */
static inline bool button_text_is_image(const button_text_t* t) {
    return t->character == 0 && t->image_path[0] != '\0';
}

void ui_render_text(lv_obj_t * obj, const char * text, const lv_style_t * default_style, uint8_t base_font_size, bool bold);
size_t parse_text(const char * text, size_t len, button_text_t * button_text);

#endif //BUTTON_UI_TEXT_H
