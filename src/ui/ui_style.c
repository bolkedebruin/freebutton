// See ui_style.h.
#include "ui_style.h"
#include "fonts/fonts.h"

#include <stdbool.h>

static lv_style_t styles[STYLE_EL_COUNT];
static bool styles_ready; /* lv_style_init once only (bug-049) */
static bool applied;      /* the styles hold properties */

static style_props_t resolved_now[STYLE_EL_COUNT];
static bool resolved_set;

static void ensure_resolved(void) {
    if (!resolved_set) {
        style_logic_resolve(NULL, resolved_now);
        resolved_set = true;
    }
}

/* The element's text_align when "style" sets none: the keys centre their
 * labels and the list is left aligned (as before "style"); display text
 * is aligned by its markup (\a) and the item "align". */
static bool element_align(const style_element_t el, lv_text_align_t *out) {
    switch (el) {
        case STYLE_EL_KEY_LABEL:
        case STYLE_EL_KEY_TOPLABEL: *out = LV_TEXT_ALIGN_CENTER; return true;
        case STYLE_EL_LIST: *out = LV_TEXT_ALIGN_LEFT; return true;
        default: return false;
    }
}

static lv_text_align_t to_lv_align(const uint8_t a) {
    switch (a) {
        case STYLE_ALIGN_CENTER: return LV_TEXT_ALIGN_CENTER;
        case STYLE_ALIGN_RIGHT: return LV_TEXT_ALIGN_RIGHT;
        default: return LV_TEXT_ALIGN_LEFT;
    }
}

/* Which properties go into the style. Every element: what it resolved to.
 * list.selected (LV_PART_SELECTED): its bg and text colour, plus only the
 * text properties that differ from the list's, i.e. that list.selected
 * sets itself; the rest LVGL takes from MAIN (an inheritable property of
 * another part falls back to MAIN), so an item font on the list also
 * reaches the selected row. */
static uint16_t style_props_of(const style_element_t el, const style_props_t *r) {
    if (el != STYLE_EL_LIST_SELECTED) {
        return r->has;
    }
    const style_props_t *l = &resolved_now[STYLE_EL_LIST];
    uint16_t has = r->has & (STYLE_HAS_COLOR | STYLE_HAS_BG_COLOR | STYLE_HAS_BG_OPA);
    if ((r->has & (STYLE_HAS_FONT | STYLE_HAS_SIZE)) == (STYLE_HAS_FONT | STYLE_HAS_SIZE) &&
        (r->face != l->face || r->px != l->px)) {
        has |= STYLE_HAS_FONT | STYLE_HAS_SIZE;
    }
    if ((r->has & STYLE_HAS_OPA) && (!(l->has & STYLE_HAS_OPA) || r->opa != l->opa)) has |= STYLE_HAS_OPA;
    if ((r->has & STYLE_HAS_ALIGN) && (!(l->has & STYLE_HAS_ALIGN) || r->align != l->align)) has |= STYLE_HAS_ALIGN;
    if ((r->has & STYLE_HAS_LETTER_SPACE) && (!(l->has & STYLE_HAS_LETTER_SPACE) || r->letter_space != l->letter_space))
        has |= STYLE_HAS_LETTER_SPACE;
    if ((r->has & STYLE_HAS_LINE_SPACE) && (!(l->has & STYLE_HAS_LINE_SPACE) || r->line_space != l->line_space))
        has |= STYLE_HAS_LINE_SPACE;
    if ((r->has & STYLE_HAS_DECOR) && (!(l->has & STYLE_HAS_DECOR) || r->decor != l->decor)) has |= STYLE_HAS_DECOR;
    return has;
}

static void fill(const style_element_t el) {
    lv_style_t *s = &styles[el];
    const style_props_t *r = &resolved_now[el];
    const uint16_t has = style_props_of(el, r);

    /* text_font + text_size: one instance. display.unit has none by default
     * (its span labels get the value's, ui_box.c). */
    if ((has & (STYLE_HAS_FONT | STYLE_HAS_SIZE)) == (STYLE_HAS_FONT | STYLE_HAS_SIZE)) {
        lv_style_set_text_font(s, fonts_get(r->face, r->px));
    } else {
        lv_style_remove_prop(s, LV_STYLE_TEXT_FONT);
    }
    if (has & STYLE_HAS_COLOR) {
        lv_style_set_text_color(s, lv_color_hex(r->color));
    } else {
        lv_style_remove_prop(s, LV_STYLE_TEXT_COLOR);
    }
    if (has & STYLE_HAS_OPA) {
        lv_style_set_text_opa(s, r->opa);
    } else {
        lv_style_remove_prop(s, LV_STYLE_TEXT_OPA);
    }
    lv_text_align_t align;
    if (has & STYLE_HAS_ALIGN) {
        lv_style_set_text_align(s, to_lv_align(r->align));
    } else if (element_align(el, &align)) {
        lv_style_set_text_align(s, align);
    } else {
        lv_style_remove_prop(s, LV_STYLE_TEXT_ALIGN);
    }
    if (has & STYLE_HAS_LETTER_SPACE) {
        lv_style_set_text_letter_space(s, r->letter_space);
    } else {
        lv_style_remove_prop(s, LV_STYLE_TEXT_LETTER_SPACE);
    }
    if (has & STYLE_HAS_LINE_SPACE) {
        lv_style_set_text_line_space(s, r->line_space);
    } else {
        lv_style_remove_prop(s, LV_STYLE_TEXT_LINE_SPACE);
    }
    if (has & STYLE_HAS_DECOR) {
        lv_style_set_text_decor(s, (lv_text_decor_t)r->decor);
    } else {
        lv_style_remove_prop(s, LV_STYLE_TEXT_DECOR);
    }
    if (has & STYLE_HAS_BG_COLOR) {
        lv_style_set_bg_color(s, lv_color_hex(r->bg_color));
    } else {
        lv_style_remove_prop(s, LV_STYLE_BG_COLOR);
    }
    if (has & STYLE_HAS_BG_OPA) {
        lv_style_set_bg_opa(s, r->bg_opa);
    } else {
        lv_style_remove_prop(s, LV_STYLE_BG_OPA);
    }
}

void ui_style_apply(const style_props_t resolved[STYLE_EL_COUNT]) {
    if (!styles_ready) {
        for (int i = 0; i < STYLE_EL_COUNT; i++) lv_style_init(&styles[i]);
        styles_ready = true;
    }
    if (resolved != NULL) {
        for (int i = 0; i < STYLE_EL_COUNT; i++) resolved_now[i] = resolved[i];
        resolved_set = true;
    } else {
        ensure_resolved();
    }
    for (int i = 0; i < STYLE_EL_COUNT; i++) fill((style_element_t)i);
    applied = true;
    for (int i = 0; i < STYLE_EL_COUNT; i++) lv_obj_report_style_change(&styles[i]);
}

lv_style_t *ui_style_get(const style_element_t el) {
    if (!applied) {
        ui_style_apply(NULL); /* no config loaded: the firmware defaults */
    }
    return &styles[el < STYLE_EL_COUNT ? el : STYLE_EL_KEY_LABEL];
}

const style_props_t *ui_style_resolved(const style_element_t el) {
    ensure_resolved();
    return &resolved_now[el < STYLE_EL_COUNT ? el : STYLE_EL_KEY_LABEL];
}
