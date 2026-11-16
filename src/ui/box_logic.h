// The font and padding rules of a display item box and of a text span,
// without LVGL: which face and px the caption, the value, the unit and a
// span of marked-up text get, and the bottom padding that goes with the
// value's size. ui_box.c and ui_text.c apply them to LVGL objects; the web
// IDE's emulator (tools/js/emulator) applies the same functions to its
// virtual display. Plain C, header only: the native tests build it
// (test/test_box_logic).
#ifndef BOX_LOGIC_H
#define BOX_LOGIC_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "fonts/font_logic.h"
#include "style_logic.h"
#include "text_spans.h"

#ifdef __cplusplus
extern "C" {
#endif

/* \s0 and \s6..\s9 were always 14 px (\s1..\s5: the legacy steps). */
#define BOX_LOGIC_SPAN_DEFAULT_PX 14

/*
 * The font of a span: its \f face, else the base face; its \s size (a
 * legacy step through style_logic_item_px, on a title element step 2 = 18
 * px; \s0 / \s6..\s9 = 14 px; or px), else the base px. A span whose \f{}
 * named an unknown font (TEXT_FONT_UNKNOWN) keeps the base face.
 */
static inline void box_logic_span_font(const text_span_t *s, const int base_face, const int base_px,
                                       const bool title, int *face, int *px)
{
    *face = s->font >= 0 ? s->font : base_face;
    *px = base_px;
    switch (s->size_kind) {
        case TEXT_SIZE_STEP:
            *px = (s->size >= 1 && s->size <= 5)
                      ? style_logic_item_px(s->size, title, BOX_LOGIC_SPAN_DEFAULT_PX)
                      : BOX_LOGIC_SPAN_DEFAULT_PX;
            break;
        case TEXT_SIZE_PX:
            *px = s->size;
            break;
        default:
            break;
    }
}

/* The base font (face, px) of an element: the item's (local layer) where it
 * sets one, else the resolved style's. fontsize 0 = none. */
static inline int box_logic_base_face(const int item_face, const style_props_t *r)
{
    return item_face != FONT_FACE_NONE ? item_face : r->face;
}

static inline int box_logic_base_px(const int item_fontsize, const bool title, const style_props_t *r)
{
    return style_logic_item_px(item_fontsize, title, r->px);
}

/* The unit: the item's "font" / "fontsize" are the value's and the unit's;
 * without them the unit has display.unit's font / size where "style" sets
 * them, else the value's. v = display.value, u = display.unit (resolved). */
static inline int box_logic_unit_face(const int item_face, const style_props_t *v, const style_props_t *u)
{
    return item_face != FONT_FACE_NONE ? item_face : (u->has & STYLE_HAS_FONT) ? u->face : v->face;
}

static inline int box_logic_unit_px(const int item_fontsize, const style_props_t *v, const style_props_t *u)
{
    return style_logic_item_px(item_fontsize, false, (u->has & STYLE_HAS_SIZE) ? u->px : v->px);
}

/* The step whose padding the value gets: the item's legacy step as it is;
 * else the value's px (the item's, or the style's without one) as the
 * nearest step's px (14/24/32/48/72). Two cases get none (0): less than 14
 * px (as the old invalid steps 6..9 always had), and 14 px that comes from
 * the style because the item has no size (as the old default fontsize 14
 * always had). An item's own "fontsize": 14 is step 1, so a pixel size can
 * replace every legacy step. value_style_px = display.value's resolved px. */
static inline int box_logic_padding_step(const int fontsize, const int value_style_px)
{
    static const uint8_t step_px[5] = {14, 24, 32, 48, 72};
    if (fontsize >= 1 && fontsize <= 5) {
        return fontsize;
    }
    const int px = style_logic_item_px(fontsize, false, value_style_px);
    if (px < 14 || (px == 14 && fontsize == 0) || px > FONT_PX_MAX) {
        return 0;
    }
    int best = 0;
    for (int i = 1; i < 5; i++) {
        if (abs(px - step_px[i]) < abs(px - step_px[best])) {
            best = i;
        }
    }
    return best + 1;
}

/* The bottom padding (px) of the caption and of the box for a padding step
 * (box_logic_padding_step) and the item's boxtype. false for step 0: no
 * padding of its own (the caller removes one a font set at runtime left). */
static inline bool box_logic_padding(const int step, const int boxtype, int *caption_pad, int *box_pad)
{
    switch (step) {
        case 1: *caption_pad = 2; *box_pad = 1; return true;
        case 2: *caption_pad = 4; *box_pad = 3; return true;
        case 3: *caption_pad = boxtype ? 0 : 4; *box_pad = 8; return true;
        case 4: *caption_pad = boxtype ? 6 : 10; *box_pad = 8; return true;
        case 5: *caption_pad = boxtype ? 6 : 14; *box_pad = 12; return true;
        default: *caption_pad = 0; *box_pad = 0; return false;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* BOX_LOGIC_H */
