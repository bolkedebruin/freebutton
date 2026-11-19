// The font of a text span and the padding of a display item box, without
// LVGL: which face and px a span of marked-up text gets, and the bottom
// padding that goes with the value's size. (Which face and px the caption,
// the value and the unit start from is the lookup of style_logic.h.) ui_box.c and ui_text.c apply them to LVGL objects; the web
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

/* Where a text is placed in the width of its part: by the last \al, \ac or
 * \ar among its spans, else by the part's text-align (style_align: a
 * style_align_t, 0 = no style sets one). TEXT_ALIGN_KEEP (0): not aligned,
 * the part is as wide as its text. So a text that starts with \ac and the
 * same text in a part whose style says "center" are placed the same. (The
 * two enums have the same values: left 1, centre 2, right 3.) */
static inline uint8_t box_logic_text_align(const text_span_t *spans, const size_t count, const uint8_t style_align)
{
    uint8_t align = style_align;
    for (size_t i = 0; i < count; i++) {
        if (spans[i].align != TEXT_ALIGN_KEEP) {
            align = spans[i].align;
        }
    }
    return align;
}

/* The step whose padding a display item gets: the value's resulting size
 * (px: the lookup through the item's styles, "displayitem" and the
 * built-in 14) as the nearest of the steps 14 / 24 / 32 / 48 / 72, a tie to
 * the smaller. Two cases get none (0): less than 14 px, and exactly 14 px
 * that none of the item's own styles set (own_size false: the size comes
 * from "displayitem" or is the built-in one), so that an item without a
 * style looks as it always did. An item whose own style says 14 px is step
 * 1. */
static inline int box_logic_padding_step(const int px, const bool own_size)
{
    static const uint8_t step_px[5] = {14, 24, 32, 48, 72};
    if (px < 14 || (px == 14 && !own_size) || px > FONT_PX_MAX) {
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
 * padding of its own (the caller removes one an earlier style left). */
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
