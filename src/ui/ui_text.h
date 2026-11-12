//
// Created by Bolke de Bruin on 03/07/2024.
//
// Display text with markup (text_spans.h: \f font, \s size, \c colour,
// \a / \v alignment, \u code points) drawn as one lv_label per span.
//

#ifndef BUTTON_UI_TEXT_H
#define BUTTON_UI_TEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>

/* Spans one text may have; more attribute changes than this and the last
 * span runs to the end of the text with its attributes. */
#define UI_TEXT_MAX_SPANS 16

/*
 * Replace obj's children with `text` (LVGL lock held). Each span label gets
 * `default_style` (the element's shared style, ui_style.h) and a local font.
 *  base_face: fonts.h face of the text: the item's font, else the style's.
 *  base_px:   px of the text: the item's fontsize, else the style's
 *             (style_logic_item_px).
 *  title:     a title element (the caption): \s2 = 18 px, as before.
 * A span's \f font or \s size replaces the base for that span.
 */
void ui_render_text(lv_obj_t * obj, const char * text, const lv_style_t * default_style,
                    int base_face, int base_px, bool title);

#endif //BUTTON_UI_TEXT_H
