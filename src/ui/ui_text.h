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

/* Spans one text may have; more attribute changes than this and the last
 * span runs to the end of the text with its attributes. */
#define UI_TEXT_MAX_SPANS 16

/*
 * Replace obj's children with `text` (LVGL lock held). Each span label gets
 * `default_style` (the element's shared style, ui_style.h) and a local font.
 *  named:     ref of the part's named style (ui_style.h), 0 = none. It goes
 *             on obj itself (the base colour of the spans is read there) and
 *             on each span label, after the element style, so it wins over
 *             it. obj is kept by the caller across calls and the labels are
 *             made again on every call; which named style obj carries is
 *             kept in obj's user data, which therefore belongs to this
 *             file (NULL on a new object). Without room in the LVGL pool
 *             the style is not added and the text is drawn without it. A
 *             caller that computes base_face / base_px from the named style
 *             calls ui_text_set_named() first, computes them from
 *             ui_text_named() (what obj carries) and passes that ref here,
 *             so that the font and the colour are those of one look.
 *  base_face: fonts.h face of the text: the item's font, else the named
 *             style's, else the element style's.
 *  base_px:   px of the text: the item's fontsize, else the named style's
 *             text_size, else the element style's (style_logic_font_layer,
 *             style_logic_item_px).
 *  title:     a title element (the caption): \s2 = 18 px, as before.
 * A span's \f font or \s size replaces the base for that span.
 * Returns false when the text was not drawn: the LVGL pool is too low now
 * (its reserve, see ui_text.c). The element is then empty; draw it again
 * later (DisplayItem does, through requestPendingRenderLater()).
 */
bool ui_render_text(lv_obj_t * obj, const char * text, const lv_style_t * default_style, uint8_t named,
                    int base_face, int base_px, bool title);

/* Have obj (an element ui_render_text draws into) carry the named style
 * `named` (0 = none), where it carries another now. LVGL lock held. false:
 * the LVGL pool has no room to add it now (ui_style_named_attach); obj then
 * carries none, and the caller tries again later. */
bool ui_text_set_named(lv_obj_t * obj, uint8_t named);

/* The ref of the named style obj carries now (0 = none). */
uint8_t ui_text_named(lv_obj_t * obj);

/* Since boot: texts drawn in one format, and texts not drawn, for lack of pool. */
void ui_text_stats(uint32_t * collapsed, uint32_t * skipped);

/* For a caller that draws a one-span text itself (the key displays,
 * ui_button.c). The \f{} resolver for text_parse(), and the font of a span:
 * its \f face and \s size, else the base. */
int ui_text_resolve_font(const char * name, size_t len, bool is_id, int32_t id, void * ctx);
lv_font_t * ui_text_span_font(const text_span_t * s, int base_face, int base_px, bool title);

#endif //BUTTON_UI_TEXT_H
