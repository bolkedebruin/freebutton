//
// Created by Bolke de Bruin on 03/07/2024.
//

#include "ui_text.h"
#include "lvgl.h"
#include <stdio.h>

extern lv_font_t* fnt_semibold_14;
extern lv_font_t* fnt_semibold_18;

extern lv_font_t* fnt_medium_14;
extern lv_font_t* fnt_medium_24;
extern lv_font_t* fnt_medium_32;
extern lv_font_t* fnt_medium_48;
extern lv_font_t* fnt_medium_72;

// Helper function to add characters to the output
static inline void add_text(button_text_t **output, const char *text, size_t len, uint8_t size, lv_color_t color, lv_text_align_t text_align, lv_align_t vertical_align) {
    for (size_t i = 0; i < len; i++) {
        (*output)->character = text[i];
        (*output)->size = size;
        (*output)->color = color;
        (*output)->text_align = text_align;
        (*output)->vertical_align = vertical_align;
        (*output)++;
    }
}


/*
 * Parse a text string into a button_text_t array. It is assumed that the text is a null-terminated string and
 * the button_text array is large enough to hold the parsed text, which will be equal or less than the original text.
 *
 * The text can contain the following escape sequences:
 * - \\: Escaped backslash, will be added as a literal character
 * - \uXXXX: Unicode character, where XXXX is the hexadecimal representation of the unicode character
 * - \sX: Font size, where X is a digit
 * - \cXXXXXX: Font color, where XXXXXX is the hexadecimal representation of the color
 *
 * The function returns the number of characters in the button_text array.
 *
 */
size_t parse_text(const char * text, size_t len, button_text_t * button_text) {
    const char *input = text;
    const char *end = text + len;

    button_text_t *output = button_text;
    uint8_t current_size = 0;
    lv_color_t current_color = lv_color_black();
    lv_text_align_t current_text_align = LV_TEXT_ALIGN_LEFT;
    lv_align_t current_vertical_align = LV_ALIGN_CENTER;

    if (button_text == NULL) {
        return -1;
    }

    while (input < end) {
        if (*input != '\\') {
            add_text(&output, input, 1, current_size, current_color, current_text_align, current_vertical_align);
            input++;
            continue;
        }


        input++; /* Skip the backslash */

        /* backslash at the end of input */
        if (input == end) {
            add_text(&output, "\\", 1, current_size, current_color, current_text_align, current_vertical_align);
            break;
        }


        switch (*input) {
            case '\\':
                /* Escaped backslash: Add it as a literal character */
                add_text(&output, "\\", 1, current_size, current_color, current_text_align, current_vertical_align);
                break;
            case 'a':
                /* Text alignment: \al (left), \ac (center), \ar (right) */
                if (input + 1 < end) {
                    switch (*(input + 1)) {
                        case 'l': current_text_align = LV_TEXT_ALIGN_LEFT; input++; break;
                        case 'c': current_text_align = LV_TEXT_ALIGN_CENTER; input++; break;
                        case 'r': current_text_align = LV_TEXT_ALIGN_RIGHT; input++; break;
                        default: add_text(&output, "\\a", 2, current_size, current_color, current_text_align, current_vertical_align);
                    }
                } else {
                    /* not enough characters */
                    add_text(&output, "\\a", 2, current_size, current_color, current_text_align, current_vertical_align);
                }
                break;
            case 'v':
                /* Vertical alignment: \vt (top), \vm (middle), \vb (bottom) */
                if (input + 1 < end) {
                    switch (*(input + 1)) {
                        case 't': current_vertical_align = LV_ALIGN_TOP_MID; input++; break;
                        case 'm': current_vertical_align = LV_ALIGN_CENTER; input++; break;
                        case 'b': current_vertical_align = LV_ALIGN_BOTTOM_MID; input++; break;
                        default: add_text(&output, "\\v", 2, current_size, current_color, current_text_align, current_vertical_align);
                    }
                } else {
                    /* not enough characters */
                    add_text(&output, "\\v", 2, current_size, current_color, current_text_align, current_vertical_align);
                }
                break;
            case 'u':
                /* Unicode character: Parse 4 hex digits */
                if (input + 4 < end) {
                    char hex[5] = {0};
                    memcpy(hex, input + 1, 4);
                    uint32_t unicode = (uint32_t)strtol(hex, NULL, 16);
                    add_text(&output, (char*)&unicode, 1, current_size, current_color, current_text_align, current_vertical_align);
                    input += 4;
                } else {
                    /* not enough characters */
                    add_text(&output, "\\u", 2, current_size, current_color, current_text_align, current_vertical_align);
                }
                break;
            case 's':
                /* Font size: Parse single digit */
                if (input + 1 < end && *(input + 1) >= '0' && *(input + 1) <= '9') {
                    current_size = *(input + 1) - '0';
                    input++;
                } else {
                    /* not enough characters */
                    add_text(&output, "\\s", 2, current_size, current_color, current_text_align, current_vertical_align);
                }
                break;
            case 'c':
                /* Font color: Parse 6 hex digits */
                if (input + 6 < end) {
                    char hex[7] = {0};
                    memcpy(hex, input + 1, 6);
                    current_color = lv_color_hex(strtol(hex, NULL, 16));
                    input += 6;
                } else {
                    /* not enough characters */
                    add_text(&output, "\\c", 2, current_size, current_color, current_text_align, current_vertical_align);
                }
                break;
            default:
                // Invalid escape sequence: output the backslash and the current character
                add_text(&output, "\\", 1, current_size, current_color, current_text_align, current_vertical_align);
                add_text(&output, input, 1, current_size, current_color, current_text_align, current_vertical_align);
                break;
        }
        input++;
    }
    return output - button_text;
}

static inline bool ui_text_prop_eq(button_text_t t1, button_text_t t2)
{
    return (t1.size == t2.size)
            && lv_color_eq(t1.color, t2.color)
            && (t1.text_align == t2.text_align)
            && (t1.vertical_align == t2.vertical_align);
}

void ui_render_text(lv_obj_t * obj, const char * text, lv_style_t * default_style, uint8_t base_font_size, bool bold) {
    size_t len = lv_strlen(text);
    button_text_t * button_text = lv_malloc(len * sizeof(button_text_t));

    /* clear out any existing references */
    lv_obj_clean(obj);

    len = parse_text(text, len, button_text);

    lv_obj_t * span_group = lv_spangroup_create(obj);
    lv_obj_add_style(span_group, default_style, 0);
    // LVGL 9.3: lv_spangroup_set_mode is deprecated, use width control instead
    lv_obj_set_width(span_group, LV_SIZE_CONTENT);
    lv_spangroup_set_overflow(span_group, LV_SPAN_OVERFLOW_ELLIPSIS);

    lv_color_t base_font_color = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    uint8_t max_size = 0;

    lv_text_align_t current_text_align = LV_TEXT_ALIGN_LEFT;
    lv_align_t current_vertical_align = LV_ALIGN_CENTER;

    uint16_t i = 0;
    while (i < len) {
        lv_span_t * span = lv_spangroup_new_span(span_group);

        uint8_t current_size = button_text[i].size > 0 ? button_text[i].size : base_font_size;
        lv_color_t current_color = lv_color_eq(button_text[i].color, lv_color_black()) ? base_font_color : button_text[i].color;

        if (button_text[i].text_align != LV_TEXT_ALIGN_LEFT) {
            current_text_align = button_text[i].text_align;
        }
        if (button_text[i].vertical_align != LV_ALIGN_CENTER) {
            current_vertical_align = button_text[i].vertical_align;
        }

        /* group characters that are of equal color, size and alignment */
        uint16_t span_length = i;
        while (span_length < len
               && (button_text[span_length].size == current_size || button_text[span_length].size == 0)
               && (button_text[span_length].vertical_align == current_vertical_align || button_text[span_length].vertical_align == 0)
               && (button_text[span_length].text_align == current_text_align || button_text[span_length].text_align == 0)
               && (lv_color_eq(button_text[span_length].color, current_color) || lv_color_eq(button_text[span_length].color, lv_color_black()))) {
            span_length++;
        }

        lv_style_t * span_style = lv_span_get_style(span);
        
        switch (current_size) {
            case 1:
                bold ? lv_style_set_text_font(span_style, fnt_semibold_14) : lv_style_set_text_font(span_style, fnt_medium_14);
                break;
            case 2:
                bold ? lv_style_set_text_font(span_style, fnt_semibold_18) : lv_style_set_text_font(span_style, fnt_medium_24);
                break;
            case 3: lv_style_set_text_font(span_style, fnt_medium_32); break;
            case 4: lv_style_set_text_font(span_style, fnt_medium_48); break;
            case 5: lv_style_set_text_font(span_style, fnt_medium_72); break;
            default: lv_style_set_text_font(span_style, fnt_medium_14); break;
        }
        lv_style_set_text_color(span_style, current_color);

        /* set alignment */
        lv_style_set_text_align(span_style, current_text_align);
        lv_style_set_align(span_style, current_vertical_align);

        if (current_size > max_size) {
            max_size = current_size;
        }

        /* 4 bytes per character for unicode */
        char * buf = lv_malloc((span_length - i + 1) * sizeof(uint32_t));

        for (uint16_t j = i; j < span_length; j++) {
            buf[j - i] = button_text[j].character;
        }
        buf[span_length - i] = '\0';

        lv_span_set_text(span, buf);
        lv_free(buf);
        i = span_length;
    }

    lv_spangroup_refr_mode(span_group);

    // Apply the final text alignment to the span group
    lv_obj_set_style_text_align(span_group, current_text_align, 0);

    // Apply the final vertical alignment to the span group
    lv_obj_align(span_group, current_vertical_align, 0, 0);

    lv_free(button_text);
}
