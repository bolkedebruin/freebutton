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

#include "text_spans.h"
#include "style_logic.h"

/* Spans one text may have; more attribute changes than this and the last
 * span runs to the end of the text with its attributes. */
#define UI_TEXT_MAX_SPANS 16

/*
 * Replace obj's children with `text` (LVGL lock held). Each span label gets
 * `default_style` (the element's shared style, ui_style.h) and a local font
 * and colour.
 *  part:      which part of an item obj is (main, title or unit). obj
 *             carries the item's styles for it (ui_text_set_styles: the
 *             base colour of the spans is read from obj), and each span
 *             label gets those of them that set opacity, letter-spacing,
 *             text-decoration or text-align, after the element style.
 *  base_face: fonts.h face of the text: the lookup of ui_style.h through
 *  base_px:   the styles obj carries (ui_text_styles), so that the font and
 *             the colour are those of one look.
 *  title:     a title (the caption, a top label): \s2 = 18 px.
 *  align:     the part's text-align (style_align_t; 0 = no style sets one).
 *             The text is placed in the item's width by the last \al, \ac
 *             or \ar in it, else by this. So a text that starts with \ac
 *             and the same text with text-align "center" are drawn the same.
 * A span's \f font or \s size replaces the base for that span.
 * Returns false when the text was not drawn: the LVGL pool is too low now
 * (its reserve, see ui_text.c). The element is then empty; draw it again
 * later (DisplayItem does, through requestPendingRenderLater()).
 */
bool ui_render_text(lv_obj_t * obj, const char * text, const lv_style_t * default_style, style_part_t part,
                    int base_face, int base_px, bool title, uint8_t align);

/* Have obj (an element ui_render_text draws into, or a label) carry the
 * styles of `list` for `part`, where it carries others now. Which list obj
 * carries is kept in its user data, which therefore belongs to this file (0
 * on a new object). LVGL lock held. false: the LVGL pool has no room to add
 * them now (ui_style_attach); obj then carries what it carried, and the
 * caller tries again later. */
bool ui_text_set_styles(lv_obj_t * obj, style_list_t list, style_part_t part);

/* The same for the parts of one item together (at most
 * UI_STYLE_TARGETS_MAX): objs[i] carries the styles of `list` for
 * parts[i]. All of them get the list or none does (ui_style_attach_all), so
 * that an item never shows one part with the new styles and another with
 * the old ones. */
bool ui_text_set_styles_all(lv_obj_t * const * objs, const style_part_t * parts, size_t count, style_list_t list);

/* The style list obj carries now (0 = none). */
style_list_t ui_text_styles(lv_obj_t * obj);

/* Since boot: texts drawn in one format, and texts not drawn, for lack of pool. */
void ui_text_stats(uint32_t * collapsed, uint32_t * skipped);

/* For a caller that draws a one-span text itself (the key displays,
 * ui_button.c). The \f{} resolver for text_parse(), and the font of a span:
 * its \f face and \s size, else the base. */
int ui_text_resolve_font(const char * name, size_t len, bool is_id, int32_t id, void * ctx);
lv_font_t * ui_text_span_font(const text_span_t * s, int base_face, int base_px, bool title);

#endif //BUTTON_UI_TEXT_H
