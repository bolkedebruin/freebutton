// See ui_style.h.
#include "ui_style.h"
#include "fonts/fonts.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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
    if (style_logic_selected_own_font(l, r)) {
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

/* ---- named styles (see ui_style.h) -------------------------------------------- */

typedef struct {
    style_props_t props; /* the values now: config, then fb.style.set */
    lv_style_t style;    /* initialised once, in ui_style_named_alloc (bug-049) */
    bool filled;         /* `style` holds the properties */
} named_style_t;

/* One heap block: named_capacity entries, then the names (style_logic_names_*). */
static named_style_t *named;
static uint8_t named_capacity;
static uint8_t named_count;
static uint16_t names_room; /* bytes for the names */
static uint16_t names_used;

/* The config values of every style, from the first fb.style.set until the
 * script restarts; NULL otherwise. */
static style_props_t *named_config;
static uint16_t named_touched; /* bit i: style i differs from its config values */
static uint16_t named_dirty;   /* bit i: values changed, lv_style_t not filled yet */
static uint16_t named_changed; /* bit i: filled in the walk that runs now */
/* bit i: style i got no lv_style_t at boot (LVGL pool low). It is unknown
 * from then on: not found by name, not counted (style_logic_named_ref). */
static uint16_t named_dropped;

/* The LVGL pool also serves drawing: this much stays free after a named
 * style is filled or added to an object (as ui_text.c keeps for its
 * labels). */
#define NAMED_POOL_RESERVE 4096u
#define NAMED_POOL_BLOCK 128u /* a style's properties, or an object's style list, with margin */

static bool named_pool_room(void) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    return m.free_size >= NAMED_POOL_RESERVE + NAMED_POOL_BLOCK && m.free_biggest_size >= NAMED_POOL_BLOCK;
}

static char *names_block(void) { return (char *)(named + named_capacity); }

static named_style_t *named_at(const uint8_t ref) {
    return (ref >= 1 && ref <= named_count && named[ref - 1].filled) ? &named[ref - 1] : NULL;
}

bool ui_style_named_alloc(size_t count, const size_t name_bytes) {
    for (size_t i = 0; i < named_capacity; i++) lv_style_reset(&named[i].style);
    free(named);
    free(named_config);
    named = NULL;
    named_config = NULL;
    named_capacity = named_count = 0;
    names_room = names_used = 0;
    named_touched = named_dirty = named_changed = named_dropped = 0;
    if (count == 0) {
        return true;
    }
    if (count > STYLE_NAMED_MAX) {
        count = STYLE_NAMED_MAX;
    }
    named = (named_style_t *)malloc(count * sizeof(named_style_t) + name_bytes);
    if (named == NULL) {
        return false;
    }
    memset(named, 0, count * sizeof(named_style_t));
    for (size_t i = 0; i < count; i++) lv_style_init(&named[i].style);
    named_capacity = (uint8_t)count;
    names_room = (uint16_t)name_bytes;
    return true;
}

style_props_t *ui_style_named_add(const char *name, const size_t len) {
    if (named_count >= named_capacity || names_used + 1u + len > names_room) {
        return NULL;
    }
    names_used = (uint16_t)style_logic_names_add(names_block(), names_used, name, len);
    return &named[named_count++].props;
}

/* A named style holds exactly the properties it sets (the rest comes from
 * the element style below it). text_font + text_size: one instance; with
 * one of the two the part's font is a local style (style_logic_font_local).
 * Filling again (fb.style.set) writes the same properties, so the pool
 * block of the style does not grow. */
static void fill_named(named_style_t *n) {
    lv_style_t *s = &n->style;
    const style_props_t *p = &n->props;
    if ((p->has & (STYLE_HAS_FONT | STYLE_HAS_SIZE)) == (STYLE_HAS_FONT | STYLE_HAS_SIZE)) {
        lv_style_set_text_font(s, fonts_get(p->face, p->px));
    }
    if (p->has & STYLE_HAS_COLOR) lv_style_set_text_color(s, lv_color_hex(p->color));
    if (p->has & STYLE_HAS_OPA) lv_style_set_text_opa(s, p->opa);
    if (p->has & STYLE_HAS_ALIGN) lv_style_set_text_align(s, to_lv_align(p->align));
    if (p->has & STYLE_HAS_LETTER_SPACE) lv_style_set_text_letter_space(s, p->letter_space);
    if (p->has & STYLE_HAS_LINE_SPACE) lv_style_set_text_line_space(s, p->line_space);
    if (p->has & STYLE_HAS_DECOR) lv_style_set_text_decor(s, (lv_text_decor_t)p->decor);
    if (p->has & STYLE_HAS_BG_COLOR) lv_style_set_bg_color(s, lv_color_hex(p->bg_color));
    if (p->has & STYLE_HAS_BG_OPA) lv_style_set_bg_opa(s, p->bg_opa);
    n->filled = true;
}

uint16_t ui_style_named_apply(void) {
    for (size_t i = 0; i < named_count; i++) {
        /* lv_style_set_prop fails silently on a full pool: check first */
        if (named[i].props.has != 0 && !named_pool_room()) {
            named_dropped |= (uint16_t)(1u << i);
            continue;
        }
        fill_named(&named[i]);
    }
    return named_dropped;
}

size_t ui_style_named_count(void) { return style_logic_named_usable(named_count, named_dropped); }

size_t ui_style_named_bytes(void) { return named_capacity * sizeof(named_style_t) + names_room; }

uint8_t ui_style_named_find(const char *name, const size_t len) {
    if (named_count == 0) {
        return 0;
    }
    return style_logic_named_ref(names_block(), named_count, named_dropped, name, len);
}

const char *ui_style_named_name(const uint8_t ref, size_t *len) {
    if (ref == 0 || named_count == 0) {
        return NULL;
    }
    return style_logic_names_get(names_block(), named_count, (size_t)ref - 1u, len);
}

const style_props_t *ui_style_named_props(const uint8_t ref) {
    const named_style_t *n = named_at(ref);
    return n != NULL ? &n->props : NULL;
}

const lv_style_t *ui_style_named_get(const uint8_t ref) {
    const named_style_t *n = named_at(ref);
    return n != NULL ? &n->style : NULL;
}

bool ui_style_named_attach(lv_obj_t *obj, uint8_t *have, const uint8_t want, const lv_style_selector_t selector) {
    if (*have == want) {
        return true;
    }
    const named_style_t *old = named_at(*have);
    const named_style_t *now = named_at(want);
    if (old != NULL && now != NULL) {
        /* in place: the same position in the style list, no allocation */
        lv_obj_replace_style(obj, &old->style, &now->style, selector);
        *have = want;
        return true;
    }
    if (old != NULL) {
        lv_obj_remove_style(obj, &old->style, selector);
    }
    *have = 0;
    if (now == NULL) {
        return true; /* none, or a ref that is no style: nothing to wait for */
    }
    if (!named_pool_room()) {
        static bool warned = false;
        if (!warned) {
            LV_LOG_WARN("style: LVGL pool low, a named style was not added; tried again later (warned once)");
            warned = true;
        }
        return false;
    }
    lv_obj_add_style(obj, &now->style, selector);
    *have = want;
    return true;
}

static style_prop_status_t named_set(const uint8_t ref, const char *prop, const char *string, const int32_t number) {
    named_style_t *n = named_at(ref);
    if (n == NULL) {
        return STYLE_PROP_UNKNOWN;
    }
    style_props_t next = n->props;
    const style_prop_status_t st = string != NULL ? style_logic_named_set_string(&next, prop, string)
                                                  : style_logic_named_set_number(&next, prop, number);
    if (st != STYLE_PROP_OK) {
        return st;
    }
    if (named_config == NULL) {
        named_config = (style_props_t *)malloc(named_count * sizeof(style_props_t));
        if (named_config == NULL) {
            return STYLE_PROP_NO_MEMORY;
        }
        for (size_t i = 0; i < named_count; i++) named_config[i] = named[i].props;
    }
    n->props = next;
    named_touched |= (uint16_t)(1u << (ref - 1));
    named_dirty |= (uint16_t)(1u << (ref - 1));
    return STYLE_PROP_OK;
}

style_prop_status_t ui_style_named_set_string(const uint8_t ref, const char *prop, const char *value) {
    return value != NULL ? named_set(ref, prop, value, 0) : STYLE_PROP_BAD_VALUE;
}

style_prop_status_t ui_style_named_set_number(const uint8_t ref, const char *prop, const int32_t value) {
    return named_set(ref, prop, NULL, value);
}

void ui_style_named_restore(void) {
    if (named_config == NULL) {
        return;
    }
    for (size_t i = 0; i < named_count; i++) {
        if (named_touched & (1u << i)) named[i].props = named_config[i];
    }
    named_dirty |= named_touched;
    named_touched = 0;
    free(named_config);
    named_config = NULL;
}

void ui_style_named_refresh(void) {
    named_changed = named_dirty;
    named_dirty = 0;
    for (size_t i = 0; i < named_count; i++) {
        if ((named_changed & (1u << i)) && named[i].filled) {
            fill_named(&named[i]);
            lv_obj_report_style_change(&named[i].style);
        }
    }
}

void ui_style_named_refresh_done(void) { named_changed = 0; }

bool ui_style_named_changed(const uint8_t ref) {
    return ref >= 1 && ref <= STYLE_NAMED_MAX && (named_changed & (1u << (ref - 1))) != 0;
}
