//
// Created by Bolke de Bruin on 03/07/2024.
//
// Display text: the markup is parsed into spans (text_spans.c), and each
// span becomes one lv_label in a row container. LVGL pool per render: the
// text without its escapes (len + 1 bytes, freed again before returning)
// plus the objects; the spans live on the stack.
//

#include "ui_text.h"
#include "text_spans.h"
#include "style_logic.h"
#include "ui_style.h"
#include "box_logic.h"
#include "fonts/fonts.h"
#include "lvgl.h"

#include <string.h>

int ui_text_resolve_font(const char *name, const size_t len, const bool is_id, const int32_t id, void *ctx) {
    (void)ctx;
    const int face = is_id ? fonts_find_id(id) : fonts_find(name, len);
    return face >= 0 ? face : TEXT_FONT_UNKNOWN;
}

/*
 * The font of a span: its \f face and \s size, else the base
 * (box_logic_span_font).
 */
lv_font_t *ui_text_span_font(const text_span_t *s, const int base_face, const int base_px, const bool title) {
    static bool unknown_warned = false;
    if (s->font == TEXT_FONT_UNKNOWN && !unknown_warned) {
        LV_LOG_WARN("text: \\f{} names an unknown font; the item's font is used (warned once)");
        unknown_warned = true;
    }
    int face;
    int px;
    box_logic_span_font(s, base_face, base_px, title, &face, &px);
    return fonts_get(face, px);
}

/* Span labels come from the LVGL pool (~120 B each, measured on a device),
 * which also has to serve LVGL's drawing: a failed pool allocation there
 * halts the LVGL task (LV_ASSERT_MALLOC). A value is rendered with one label
 * per span only while UI_TEXT_POOL_RESERVE stays free afterwards; else as
 * one label in the item's font (formatting lost, text kept); else not at
 * all. Seen on a device: two 16-span values with ~6 KB free left ~2.7 KB
 * and the LVGL task halted at the next redraw. */
#define UI_TEXT_POOL_RESERVE 4096u
#define UI_TEXT_LABEL_POOL 256u /* one label with its local styles and text, with margin */

static bool pool_room_for(const size_t labels) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    return m.free_size >= UI_TEXT_POOL_RESERVE + labels * UI_TEXT_LABEL_POOL &&
           m.free_biggest_size >= UI_TEXT_LABEL_POOL;
}

static lv_text_align_t to_lv_align(const uint8_t a) {
    switch (a) {
        case TEXT_ALIGN_CENTER: return LV_TEXT_ALIGN_CENTER;
        case TEXT_ALIGN_RIGHT: return LV_TEXT_ALIGN_RIGHT;
        default: return LV_TEXT_ALIGN_LEFT;
    }
}

static lv_align_t to_lv_valign(const uint8_t v) {
    switch (v) {
        case TEXT_VALIGN_TOP: return LV_ALIGN_TOP_MID;
        case TEXT_VALIGN_BOTTOM: return LV_ALIGN_BOTTOM_MID;
        default: return LV_ALIGN_CENTER;
    }
}

/* Since boot: texts drawn in one format for lack of pool, and texts not
 * drawn at all (the caller draws those again later). */
static uint32_t g_collapsed;
static uint32_t g_skipped;

void ui_text_stats(uint32_t *collapsed, uint32_t *skipped) {
    *collapsed = g_collapsed;
    *skipped = g_skipped;
}

style_list_t ui_text_styles(lv_obj_t *obj) { return (style_list_t)(uintptr_t)lv_obj_get_user_data(obj); }

/* The item's styles on the element itself. obj already carries the element
 * style, and a style added later wins among added styles, so they go over
 * it. The list obj carries now is its user data (see ui_text.h): only a
 * change touches the object. */
bool ui_text_set_styles(lv_obj_t *obj, const style_list_t list, const style_part_t part) {
    return ui_text_set_styles_all(&obj, &part, 1, list);
}

bool ui_text_set_styles_all(lv_obj_t *const *objs, const style_part_t *parts, size_t count,
                            const style_list_t list) {
    style_list_t carried[UI_STYLE_TARGETS_MAX];
    ui_style_target_t targets[UI_STYLE_TARGETS_MAX];
    if (count > UI_STYLE_TARGETS_MAX) count = UI_STYLE_TARGETS_MAX;
    for (size_t i = 0; i < count; i++) {
        carried[i] = ui_text_styles(objs[i]);
        targets[i].obj = objs[i];
        targets[i].have = &carried[i];
        targets[i].part = parts[i];
        targets[i].selector = 0;
    }
    /* Also when every object carries `list` already: a block of one of its
     * styles may hold its first LVGL property since (ui_style.h). */
    const bool attached = ui_style_attach_all(targets, count, list);
    for (size_t i = 0; i < count; i++) lv_obj_set_user_data(objs[i], (void *)(uintptr_t)carried[i]);
    return attached;
}

bool ui_render_text(lv_obj_t *obj, const char *text, const lv_style_t *default_style, const style_part_t part,
                    const int base_face, const int base_px, const bool title, const uint8_t align) {
    /* The span labels get the styles obj carries: when the pool had no room
     * to add a new list to obj, the whole text is drawn with the old one. */
    const style_list_t carried = ui_text_styles(obj);

    /* clear out any existing references */
    lv_obj_clean(obj);

    const size_t len = text != NULL ? strlen(text) : 0;
    char *buf = (char *)lv_malloc(len + 1);
    if (buf == NULL) {
        LV_LOG_WARN("text: no LVGL memory to render %u bytes", (unsigned)len);
        g_skipped++;
        return false;
    }
    text_span_t spans[UI_TEXT_MAX_SPANS];
    text_parse_result_t parsed =
        text_parse(text != NULL ? text : "", len, buf, spans, UI_TEXT_MAX_SPANS, ui_text_resolve_font, NULL);

    /* + 1: the row container */
    if (parsed.span_count > 1 && !pool_room_for(parsed.span_count + 1)) {
        if ((g_collapsed++ & 63u) == 0) {
            LV_LOG_WARN("text: LVGL pool low, %u spans drawn as one (%lu so far)", (unsigned)parsed.span_count,
                        (unsigned long)g_collapsed);
        }
        const text_span_t first = spans[0];
        memset(&spans[0], 0, sizeof(spans[0]));
        spans[0].offset = 0;
        spans[0].len = (uint16_t)parsed.text_len;
        spans[0].font = TEXT_FONT_BASE;
        spans[0].size_kind = TEXT_SIZE_BASE;
        spans[0].align = first.align;
        spans[0].valign = first.valign;
        parsed.span_count = 1;
    }
    if (parsed.span_count > 0 && !pool_room_for(2)) {
        if ((g_skipped++ & 63u) == 0) {
            LV_LOG_WARN("text: LVGL pool low, value not drawn (%lu so far)", (unsigned long)g_skipped);
        }
        lv_free(buf);
        return false;
    }

    /* A row container for the spans */
    lv_obj_t *container = lv_obj_create(obj);
    if (container == NULL) {
        lv_free(buf);
        g_skipped++;
        return false;
    }
    lv_obj_set_scrollable(container, false); /* no scrollbar when wider than the parent */
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_gap(container, 2, 0); /* Small gap between elements */

    const lv_color_t base_color = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    /* The last \\al, \\ac or \\ar of the text, else the part's text-align
     * (box_logic.h); none: left, and the part is as wide as its text. */
    const uint8_t placed = box_logic_text_align(spans, parsed.span_count, align);
    const lv_text_align_t text_align = to_lv_align(placed);
    const bool aligned = placed != TEXT_ALIGN_KEEP;
    lv_align_t vertical_align = LV_ALIGN_CENTER;

    for (size_t i = 0; i < parsed.span_count; i++) {
        const text_span_t *s = &spans[i];
        if (s->valign != TEXT_VALIGN_KEEP) {
            vertical_align = to_lv_valign(s->valign);
        }

        lv_obj_t *label = lv_label_create(container);
        if (label == NULL) {
            break;
        }
        lv_obj_add_style(label, default_style, 0);
        /* after the element style (they win); 8 bytes each, of the label's
         * pool share checked above */
        ui_style_add_to_span(label, carried, part);
        lv_obj_set_style_text_font(label, ui_text_span_font(s, base_face, base_px, title), 0);
        lv_obj_set_style_text_color(label, s->has_color ? lv_color_hex(s->color) : base_color, 0);

        /* The span's bytes as a C string, in place: the label copies them */
        char *end = buf + s->offset + s->len;
        const char saved = *end;
        *end = '\0';
        lv_label_set_text(label, buf + s->offset);
        *end = saved;
    }

    /* Apply alignment to container */
    lv_obj_set_style_text_align(container, text_align, 0);
    if (align != 0 && placed != align) {
        /* The text's own alignment over a style's: the labels carry the
         * style, whose text-align would win over the container's there. */
        const uint32_t labels = lv_obj_get_child_count(container);
        for (uint32_t i = 0; i < labels; i++) {
            lv_obj_set_style_text_align(lv_obj_get_child(container, (int32_t)i), text_align, 0);
        }
    }
    lv_obj_align(container, vertical_align, 0, 0);

    /* An alignment (a style's text-align, or \\al \\ac \\ar) in a display
     * item: its caption, value and unit are flex rows as wide as their
     * content, so the row of spans has nowhere to move. With an alignment the
     * element takes the item's width and places the row on its main axis;
     * without one it is as wide as its text. (A key display's row is
     * not in a flex parent: lv_obj_align above centres it.) */
    if (lv_obj_get_style_layout(obj, LV_PART_MAIN) == LV_LAYOUT_FLEX) {
        if (aligned) {
            lv_obj_set_width(obj, lv_pct(100));
            lv_obj_set_style_flex_main_place(obj,
                                             text_align == LV_TEXT_ALIGN_CENTER ? LV_FLEX_ALIGN_CENTER
                                             : text_align == LV_TEXT_ALIGN_RIGHT ? LV_FLEX_ALIGN_END
                                                                                  : LV_FLEX_ALIGN_START,
                                             0);
        } else if (lv_obj_remove_local_style_prop(obj, LV_STYLE_FLEX_MAIN_PLACE, 0)) {
            lv_obj_set_width(obj, LV_SIZE_CONTENT); /* it was aligned before */
        }
    }

    lv_free(buf);
    return true;
}
