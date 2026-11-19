// See ui_style.h.
#include "ui_style.h"
#include "fonts/fonts.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define STYLE_HAS_FONT_SIZE (STYLE_HAS_FONT | STYLE_HAS_SIZE)

/* ---- the kinds ---------------------------------------------------------------- */

static lv_style_t styles[STYLE_EL_COUNT];
static bool styles_ready; /* lv_style_init once only (bug-049) */
static bool applied;      /* the styles hold properties */

static style_props_t resolved_now[STYLE_EL_COUNT];
static uint32_t kind_icon[STYLE_KIND_COUNT];
static bool resolved_set;

static void ensure_resolved(void) {
    if (!resolved_set) {
        style_logic_resolve(NULL, resolved_now);
        resolved_set = true;
    }
}

/* What a part without text-align does: the button labels are centred and
 * the list is left aligned; a display item part is as wide as its text. */
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

/* The properties of `has` into `s`. `remove`: a property not in `has` is
 * taken out (an element style that is filled again at a config load); an
 * item's style always holds the same properties, so filling it again
 * (fb.style.set) does not grow its block in the pool. */
static void fill_style(lv_style_t *s, const style_props_t *p, const uint16_t has, const bool remove) {
    /* font-family + font-size: one instance */
    if ((has & STYLE_HAS_FONT_SIZE) == STYLE_HAS_FONT_SIZE) {
        lv_style_set_text_font(s, fonts_get(p->face, p->px));
    } else if (remove) {
        lv_style_remove_prop(s, LV_STYLE_TEXT_FONT);
    }
    if (has & STYLE_HAS_COLOR) {
        lv_style_set_text_color(s, lv_color_hex(p->color));
    } else if (remove) {
        lv_style_remove_prop(s, LV_STYLE_TEXT_COLOR);
    }
    if (has & STYLE_HAS_OPA) {
        lv_style_set_text_opa(s, p->opa);
    } else if (remove) {
        lv_style_remove_prop(s, LV_STYLE_TEXT_OPA);
    }
    if (has & STYLE_HAS_ALIGN) {
        lv_style_set_text_align(s, to_lv_align(p->align));
    } else if (remove) {
        lv_style_remove_prop(s, LV_STYLE_TEXT_ALIGN);
    }
    if (has & STYLE_HAS_LETTER_SPACE) {
        lv_style_set_text_letter_space(s, p->letter_space);
    } else if (remove) {
        lv_style_remove_prop(s, LV_STYLE_TEXT_LETTER_SPACE);
    }
    if (has & STYLE_HAS_DECOR) {
        lv_style_set_text_decor(s, (lv_text_decor_t)p->decor);
    } else if (remove) {
        lv_style_remove_prop(s, LV_STYLE_TEXT_DECOR);
    }
    if (has & STYLE_HAS_BG) {
        lv_style_set_bg_color(s, lv_color_hex(p->bg & 0xFFFFFFu));
        lv_style_set_bg_opa(s, (lv_opa_t)(p->bg >> 24));
    } else if (remove) {
        lv_style_remove_prop(s, LV_STYLE_BG_COLOR);
        lv_style_remove_prop(s, LV_STYLE_BG_OPA);
    }
}

/* An element style holds what a block of an item's style would
 * (style_logic_lvgl_has), and "list" also its background: only rollers
 * carry that style. */
static void fill_element(const style_element_t el) {
    static const style_part_t part_of[STYLE_EL_COUNT] = {
        STYLE_PART_MAIN, STYLE_PART_TITLE, STYLE_PART_MAIN, STYLE_PART_UNIT, STYLE_PART_TITLE,
        STYLE_PART_MAIN, STYLE_PART_SELECTED,
    };
    const style_props_t *r = &resolved_now[el];
    uint16_t has = style_logic_lvgl_has(r, part_of[el]);
    if (el == STYLE_EL_LIST) has |= (uint16_t)(r->has & STYLE_HAS_BG);
    fill_style(&styles[el], r, has, true);
    lv_text_align_t align;
    if (!(has & STYLE_HAS_ALIGN) && element_align(el, &align)) {
        lv_style_set_text_align(&styles[el], align);
    }
}

void ui_style_apply(const style_props_t resolved[STYLE_EL_COUNT], const uint32_t icon[STYLE_KIND_COUNT]) {
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
    for (int k = 0; k < STYLE_KIND_COUNT; k++) kind_icon[k] = icon != NULL ? icon[k] : 0;
    for (int i = 0; i < STYLE_EL_COUNT; i++) fill_element((style_element_t)i);
    applied = true;
    for (int i = 0; i < STYLE_EL_COUNT; i++) lv_obj_report_style_change(&styles[i]);
}

lv_style_t *ui_style_get(const style_element_t el) {
    if (!applied) {
        ui_style_apply(NULL, NULL); /* no config loaded: the built-in look */
    }
    return &styles[el < STYLE_EL_COUNT ? el : STYLE_EL_KEY_LABEL];
}

const style_props_t *ui_style_resolved(const style_element_t el) {
    ensure_resolved();
    return &resolved_now[el < STYLE_EL_COUNT ? el : STYLE_EL_KEY_LABEL];
}

/* ---- the table of the other styles (see ui_style.h) ---------------------------- */

typedef struct {
    uint8_t part[STYLE_PART_COUNT]; /* index + 1 of the part's entry, 0 = the style has no such block */
    uint32_t icon;                  /* STYLE_ICON_SET | 0xRRGGBB, 0 = no "icon" colour */
} style_head_t;

typedef struct {
    style_props_t props; /* the values now: config, then fb.style.set */
    lv_style_t style;    /* initialised once, in ui_style_table_alloc (bug-049) */
    uint8_t part;        /* style_part_t */
    uint8_t filled;      /* ENTRY_*: does `style` hold properties, and so go on the objects? */
} style_entry_t;

/* style_entry_t.filled */
enum {
    ENTRY_EMPTY = 0,  /* the block has nothing for LVGL: its style is on no object */
    ENTRY_FILLED,     /* it holds properties, and the objects that use the style carry it */
    ENTRY_FILLED_NOW, /* it got its first property in the walk that runs now (fb.style.set): its
                         users attach it in this walk (ui_style_attach_all); ENTRY_FILLED after it */
};

/* One heap block: head_capacity heads, entry_capacity entries, the names
 * (style_logic_names_*). */
static style_head_t *heads;
static style_entry_t *entries;
static uint8_t head_capacity;
static uint8_t head_count;
static uint8_t entry_capacity;
static uint8_t entry_count;
static uint16_t names_room; /* bytes for the names */
static uint16_t names_used;

/* The config values of every entry, every element and every icon colour,
 * from the first fb.style.set until the script restarts; NULL otherwise. */
static void *config_copy;
/* One bit per style (style_logic.h, "which styles a script changed"): bit i
 * = the style of the table at index i, STYLE_CHANGED_KIND(k) = the style of
 * kind k. */
static uint32_t touched;       /* differs from its config values */
static uint32_t dirty;         /* values changed, lv_style_t not filled yet */
static uint32_t changed_now;   /* filled in the walk that runs now */
/* Of changed_now: the styles of the table of which a block began or stopped
 * to hold something for LVGL in this walk (a script added the first such
 * property, or a restart took it away). The objects that use such a style
 * carry the wrong set of lv_style_t until they are attached again
 * (ui_style_attach_all). */
static uint32_t reattach_now;
/* bit i: style i got no lv_style_t at boot (LVGL pool low). It is unknown
 * from then on: not found by name, not counted (style_logic_named_ref). */
static uint32_t dropped;

/* The LVGL pool also serves drawing: this much stays free after a style is
 * filled or added to an object (as ui_text.c keeps for its labels). */
#define STYLE_POOL_RESERVE 4096u
#define STYLE_POOL_BLOCK 128u /* a style's properties, or an object's style list, with margin */

static bool pool_room(const size_t blocks) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    return m.free_size >= STYLE_POOL_RESERVE + blocks * STYLE_POOL_BLOCK && m.free_biggest_size >= STYLE_POOL_BLOCK;
}

/* Room to change the style lists of `lists` objects that grow by `grow`
 * styles together. A list that does not grow needs no reserve (it gives
 * back what it takes), but its block is still allocated again: the largest
 * free block has to hold each of them, or lv_obj_add_style halts the LVGL
 * task (LV_ASSERT_MALLOC). */
static bool pool_room_lists(const size_t grow, const size_t lists) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    if (grow != 0 && m.free_size < STYLE_POOL_RESERVE + grow * STYLE_POOL_BLOCK) return false;
    return m.free_biggest_size >= lists * STYLE_POOL_BLOCK;
}

static char *names_block(void) { return (char *)(entries + entry_capacity); }

static bool ref_usable(const uint8_t ref) {
    return ref >= 1 && ref <= head_count && !(dropped & (1u << (ref - 1)));
}

/* The entry of a style's part, NULL when the style is unknown or dropped,
 * or has no such block. */
static style_entry_t *entry_at(const uint8_t ref, const style_part_t part) {
    if (!ref_usable(ref) || (unsigned)part >= STYLE_PART_COUNT) return NULL;
    const uint8_t e = heads[ref - 1].part[part];
    return e != 0 ? &entries[e - 1] : NULL;
}

/* The entry whose lv_style_t goes on an object of `part`, NULL when the
 * block holds nothing for LVGL. */
static style_entry_t *attachable(const uint8_t ref, const style_part_t part) {
    style_entry_t *e = entry_at(ref, part);
    return e != NULL && e->filled != ENTRY_EMPTY ? e : NULL;
}

bool ui_style_table_alloc(size_t style_count, size_t parts, const size_t name_bytes) {
    for (size_t i = 0; i < entry_capacity; i++) lv_style_reset(&entries[i].style);
    free(heads);
    free(config_copy);
    heads = NULL;
    entries = NULL;
    config_copy = NULL;
    head_capacity = head_count = 0;
    entry_capacity = entry_count = 0;
    names_room = names_used = 0;
    touched = dirty = changed_now = dropped = 0;
    if (style_count == 0) {
        return true;
    }
    if (style_count > STYLE_NAMED_MAX) {
        style_count = STYLE_NAMED_MAX;
    }
    if (parts > style_count * STYLE_PART_COUNT) {
        parts = style_count * STYLE_PART_COUNT;
    }
    const size_t head_bytes = style_count * sizeof(style_head_t);
    const size_t entry_bytes = parts * sizeof(style_entry_t);
    void *block = malloc(head_bytes + entry_bytes + name_bytes);
    if (block == NULL) {
        return false;
    }
    memset(block, 0, head_bytes + entry_bytes);
    heads = (style_head_t *)block;
    entries = (style_entry_t *)((char *)block + head_bytes);
    for (size_t i = 0; i < parts; i++) lv_style_init(&entries[i].style);
    head_capacity = (uint8_t)style_count;
    entry_capacity = (uint8_t)parts;
    names_room = (uint16_t)name_bytes;
    return true;
}

uint8_t ui_style_table_add(const char *name, const size_t len) {
    if (head_count >= head_capacity || names_used + 1u + len > names_room) {
        return 0;
    }
    names_used = (uint16_t)style_logic_names_add(names_block(), names_used, name, len);
    return ++head_count;
}

style_props_t *ui_style_table_part(const uint8_t ref, const style_part_t part) {
    if (ref < 1 || ref > head_count || (unsigned)part >= STYLE_PART_COUNT || entry_count >= entry_capacity ||
        heads[ref - 1].part[part] != 0) {
        return NULL;
    }
    style_entry_t *e = &entries[entry_count++];
    e->part = (uint8_t)part;
    heads[ref - 1].part[part] = entry_count;
    return &e->props;
}

uint32_t *ui_style_table_icon(const uint8_t ref) {
    return ref >= 1 && ref <= head_count ? &heads[ref - 1].icon : NULL;
}

uint32_t ui_style_table_apply(void) {
    for (size_t i = 0; i < head_count; i++) {
        /* lv_style_set_prop fails silently on a full pool: check first, for
         * the whole style (a style is there with all its parts or not at
         * all) */
        size_t blocks = 0;
        for (int p = 0; p < STYLE_PART_COUNT; p++) {
            const uint8_t e = heads[i].part[p];
            if (e != 0 && style_logic_lvgl_has(&entries[e - 1].props, (style_part_t)p) != 0) blocks++;
        }
        if (blocks != 0 && !pool_room(blocks)) {
            dropped |= 1u << i;
            continue;
        }
        for (int p = 0; p < STYLE_PART_COUNT; p++) {
            const uint8_t e = heads[i].part[p];
            if (e == 0) continue;
            const uint16_t has = style_logic_lvgl_has(&entries[e - 1].props, (style_part_t)p);
            if (has == 0) continue; /* nothing for LVGL: never added to an object */
            fill_style(&entries[e - 1].style, &entries[e - 1].props, has, false);
            entries[e - 1].filled = ENTRY_FILLED;
        }
    }
    return dropped;
}

size_t ui_style_count(void) { return style_logic_named_usable(head_count, dropped); }

size_t ui_style_table_bytes(void) {
    return head_capacity * sizeof(style_head_t) + entry_capacity * sizeof(style_entry_t) + names_room;
}

uint8_t ui_style_find(const char *name, const size_t len) {
    if (head_count == 0) {
        return 0;
    }
    return style_logic_named_ref(names_block(), head_count, dropped, name, len);
}

const char *ui_style_name(const uint8_t ref, size_t *len) {
    if (ref == 0 || head_count == 0) {
        return NULL;
    }
    return style_logic_names_get(names_block(), head_count, (size_t)ref - 1u, len);
}

size_t ui_style_list_text(const style_list_t list, char *out, const size_t capacity) {
    size_t at = 0;
    for (size_t i = 0; i < STYLE_ITEM_MAX; i++) {
        size_t len = 0;
        const char *name = ui_style_name(style_logic_list_ref(list, i), &len);
        if (name == NULL || at + len + 2 > capacity) continue;
        if (at > 0) out[at++] = ' ';
        memcpy(out + at, name, len);
        at += len;
    }
    if (capacity > 0) out[at] = '\0';
    return at;
}

/* ---- an item's styles ----------------------------------------------------------- */

/* The blocks of `part` in the styles of `list`, in list order. */
static size_t layers_of(const style_list_t list, const style_part_t part, const style_props_t *layers[STYLE_ITEM_MAX]) {
    size_t n = 0;
    for (size_t i = 0; i < STYLE_ITEM_MAX; i++) {
        const style_entry_t *e = entry_at(style_logic_list_ref(list, i), part);
        if (e != NULL) layers[n++] = &e->props;
    }
    return n;
}

void ui_style_lookup(const style_kind_t kind, const style_part_t part, const style_list_t list, style_props_t *out,
                     bool *own_size) {
    const int el = style_logic_element(kind, part);
    if (el < 0) {
        memset(out, 0, sizeof(*out));
        if (own_size != NULL) *own_size = false;
        return;
    }
    const style_props_t *layers[STYLE_ITEM_MAX];
    const size_t n = layers_of(list, part, layers);
    style_logic_lookup(ui_style_resolved((style_element_t)el), layers, n, out);
    if (own_size != NULL) *own_size = style_logic_layers_set(layers, n, STYLE_HAS_SIZE);
}

bool ui_style_shared_font(const style_kind_t kind, const style_part_t part, const style_list_t list, int *face,
                          int *px) {
    const int el = style_logic_element(kind, part);
    if (el < 0) return false;
    const style_props_t *layers[STYLE_ITEM_MAX];
    const size_t n = layers_of(list, part, layers);
    return style_logic_shared_font(ui_style_resolved((style_element_t)el), layers, n, face, px);
}

bool ui_style_icon_color(const style_kind_t kind, const style_list_t list, uint32_t *rgb) {
    uint32_t layers[STYLE_ITEM_MAX];
    size_t n = 0;
    for (size_t i = 0; i < STYLE_ITEM_MAX; i++) {
        const uint8_t ref = style_logic_list_ref(list, i);
        if (ref_usable(ref)) layers[n++] = heads[ref - 1].icon;
    }
    return style_logic_icon_color((unsigned)kind < STYLE_KIND_COUNT ? kind_icon[kind] : 0, layers, n, rgb);
}

/* What one target has to do (ui_style_attach_all). */
typedef struct {
    const lv_style_t *old[STYLE_ITEM_MAX];  /* on the object now, and still holding properties */
    const lv_style_t *now[STYLE_ITEM_MAX];  /* what it has to carry */
    const lv_style_t *gone[STYLE_ITEM_MAX]; /* of the old list, holding nothing: taken off (see below) */
    uint8_t old_n;
    uint8_t now_n;
    uint8_t gone_n;
    bool skip;     /* the object carries the list already */
    bool in_place; /* each style takes the place of the one before it: no allocation */
    bool stale;    /* *have lists a block that the object does not carry yet (ENTRY_FILLED_NOW) */
} attach_plan_t;

static bool uses_reattached(const style_list_t list) {
    return reattach_now != 0 && style_logic_changed(reattach_now, (style_kind_t)STYLE_KIND_COUNT, list);
}

static void plan_attach(const ui_style_target_t *t, const style_list_t want, attach_plan_t *p) {
    const style_list_t have = *t->have;
    memset(p, 0, sizeof(*p));
    /* A block of one of these styles began or stopped to hold properties
     * in this walk: *have alone does not say what the object carries. */
    const bool forced = uses_reattached(have) || uses_reattached(want);
    p->skip = have == want && !forced;
    if (p->skip) return;
    for (size_t i = 0; i < STYLE_ITEM_MAX; i++) {
        const style_entry_t *e = entry_at(style_logic_list_ref(have, i), t->part);
        if (e != NULL) {
            if (e->filled == ENTRY_FILLED) {
                p->old[p->old_n++] = &e->style;
            } else if (e->filled == ENTRY_FILLED_NOW) {
                p->stale = true;
            } else if (forced) {
                /* It holds nothing: it lost its properties in this walk (a
                 * restart), or it never had any. Taking a style off that
                 * the object does not carry does nothing. */
                p->gone[p->gone_n++] = &e->style;
            }
        }
        e = attachable(style_logic_list_ref(want, i), t->part);
        if (e != NULL) p->now[p->now_n++] = &e->style;
    }
    /* As many as before and no style of the new list among the old ones at
     * another place: each takes the place of the one before it in the
     * object's style list. No allocation, so it also works in a full pool. */
    p->in_place = p->old_n == p->now_n;
    for (size_t i = 0; p->in_place && i < p->now_n; i++) {
        for (size_t j = 0; j < p->old_n; j++) {
            if (p->now[i] == p->old[j] && i != j) p->in_place = false;
        }
    }
}

bool ui_style_attach_all(const ui_style_target_t *targets, size_t count, const style_list_t want) {
    attach_plan_t plans[UI_STYLE_TARGETS_MAX];
    if (count > UI_STYLE_TARGETS_MAX) count = UI_STYLE_TARGETS_MAX;
    size_t grow = 0;  /* styles the objects' lists grow by, together */
    size_t lists = 0; /* objects whose list is allocated again */
    bool stale = false;
    for (size_t t = 0; t < count; t++) {
        attach_plan_t *p = &plans[t];
        plan_attach(&targets[t], want, p);
        if (p->skip) continue;
        /* Blocks that hold nothing any more come off first. That needs no
         * room, and the object then carries what `old` says. */
        for (size_t i = 0; i < p->gone_n; i++) lv_obj_remove_style(targets[t].obj, p->gone[i], targets[t].selector);
        stale = stale || p->stale;
        if (p->in_place || p->now_n == 0) continue;
        lists++;
        if (p->now_n > p->old_n) grow += (size_t)(p->now_n - p->old_n);
    }
    /* One check for every part of the item, before the first object gets a
     * style: all of them get the new list or none does. */
    if (lists != 0 && !pool_room_lists(grow, lists)) {
        static bool warned = false;
        if (!warned) {
            LV_LOG_WARN("style: LVGL pool low, an item's styles were not added; tried again later (warned once)");
            warned = true;
        }
        /* stale: *have lists a block that is not on its object, and after
         * this walk nothing would say so. Without room to add it, every
         * part carries no style of the list (taking styles off needs no
         * room): the look of the kind, whole, and *have says so, until the
         * caller's retry. */
        for (size_t t = 0; stale && t < count; t++) {
            if (plans[t].skip) continue;
            for (size_t i = 0; i < plans[t].old_n; i++) {
                lv_obj_remove_style(targets[t].obj, plans[t].old[i], targets[t].selector);
            }
            /* also a new block that an earlier call of this walk added */
            for (size_t i = 0; i < plans[t].now_n; i++) {
                lv_obj_remove_style(targets[t].obj, plans[t].now[i], targets[t].selector);
            }
            *targets[t].have = 0;
        }
        return false;
    }
    for (size_t t = 0; t < count; t++) {
        const attach_plan_t *p = &plans[t];
        if (p->skip) continue;
        lv_obj_t *obj = targets[t].obj;
        const lv_style_selector_t selector = targets[t].selector;
        if (p->in_place) {
            for (size_t i = 0; i < p->now_n; i++) {
                if (p->now[i] != p->old[i]) lv_obj_replace_style(obj, p->old[i], p->now[i], selector);
            }
        } else {
            for (size_t i = 0; i < p->old_n; i++) lv_obj_remove_style(obj, p->old[i], selector);
            for (size_t i = 0; i < p->now_n; i++) lv_obj_add_style(obj, p->now[i], selector);
        }
        *targets[t].have = want;
    }
    return true;
}

bool ui_style_attach(lv_obj_t *obj, style_list_t *have, const style_list_t want, const style_part_t part,
                     const lv_style_selector_t selector) {
    const ui_style_target_t target = {obj, have, part, selector};
    return ui_style_attach_all(&target, 1, want);
}

void ui_style_add_to_span(lv_obj_t *label, const style_list_t list, const style_part_t part) {
    for (size_t i = 0; i < STYLE_ITEM_MAX; i++) {
        const style_entry_t *e = attachable(style_logic_list_ref(list, i), part);
        if (e != NULL && style_logic_span_has(&e->props, part) != 0) lv_obj_add_style(label, &e->style, 0);
    }
}

/* ---- fb.style.set ---------------------------------------------------------------- */

/* The copy: entry_count style_props_t (the table), STYLE_EL_COUNT
 * style_props_t (the kinds), head_count icon words, STYLE_KIND_COUNT icon
 * words. */
static const style_props_t *copy_kind_props(void) { return (const style_props_t *)config_copy + entry_count; }
static const uint32_t *copy_icons(void) {
    return (const uint32_t *)((const char *)config_copy + (entry_count + STYLE_EL_COUNT) * sizeof(style_props_t));
}

static bool keep_config(void) {
    if (config_copy != NULL) {
        return true;
    }
    const size_t props_bytes = ((size_t)entry_count + STYLE_EL_COUNT) * sizeof(style_props_t);
    config_copy = malloc(props_bytes + ((size_t)head_count + STYLE_KIND_COUNT) * sizeof(uint32_t));
    if (config_copy == NULL) {
        return false;
    }
    style_props_t *props = (style_props_t *)config_copy;
    uint32_t *icons = (uint32_t *)((char *)config_copy + props_bytes);
    for (size_t i = 0; i < entry_count; i++) props[i] = entries[i].props;
    for (size_t i = 0; i < STYLE_EL_COUNT; i++) props[entry_count + i] = resolved_now[i];
    for (size_t i = 0; i < head_count; i++) icons[i] = heads[i].icon;
    for (size_t i = 0; i < STYLE_KIND_COUNT; i++) icons[head_count + i] = kind_icon[i];
    return true;
}

/* One value of a block (`props`, NULL when the style has no such block) or
 * of an icon word (`icon`, NULL when the kind has no icon; part ==
 * STYLE_PART_ICON). The order of the checks is style_logic.h's: the
 * property, then the block, then the value, then the memory. Checked on a
 * copy, so that nothing changes on an error. */
static style_prop_status_t set_value(style_props_t *props, uint32_t *icon, const style_part_t part, const char *prop,
                                     const char *string, const int32_t number) {
    if (part == STYLE_PART_ICON) {
        if (strcmp(prop, "color") != 0) return STYLE_PROP_UNKNOWN;
        if (icon == NULL) return STYLE_PROP_NO_PART;
        if (string == NULL) return STYLE_PROP_BAD_VALUE;
        uint32_t next = *icon;
        const style_prop_status_t st = style_logic_script_icon_set_string(&next, prop, string);
        if (st != STYLE_PROP_OK) return st;
        if (!keep_config()) return STYLE_PROP_NO_MEMORY;
        *icon = next; /* a tint: nothing in the LVGL pool */
        return STYLE_PROP_OK;
    }
    if (props == NULL) {
        const style_prop_status_t st = style_logic_script_settable(prop);
        return st != STYLE_PROP_OK ? st : STYLE_PROP_NO_PART;
    }
    style_props_t next = *props;
    const style_prop_status_t st = string != NULL ? style_logic_script_set_string(&next, prop, string)
                                                  : style_logic_script_set_number(&next, prop, number);
    if (st != STYLE_PROP_OK) return st;
    /* A property more: the refill (ui_style_refresh) grows the property
     * block of the lv_style_t, and lv_style_set_prop fails silently in a
     * full pool. Refused here instead, with nothing changed. */
    if (next.has != props->has && !pool_room(1)) return STYLE_PROP_NO_MEMORY;
    if (!keep_config()) return STYLE_PROP_NO_MEMORY;
    *props = next;
    return STYLE_PROP_OK;
}

static style_prop_status_t table_set(const uint8_t ref, const style_part_t part, const char *prop, const char *string,
                                     const int32_t number) {
    if (!ref_usable(ref)) {
        return STYLE_PROP_UNKNOWN;
    }
    style_entry_t *e = part == STYLE_PART_ICON ? NULL : entry_at(ref, part);
    const style_prop_status_t st =
        set_value(e != NULL ? &e->props : NULL, &heads[ref - 1].icon, part, prop, string, number);
    if (st == STYLE_PROP_OK) {
        touched |= 1u << (ref - 1);
        dirty |= 1u << (ref - 1);
    }
    return st;
}

static style_prop_status_t kind_set(const style_kind_t kind, const style_part_t part, const char *prop,
                                    const char *string, const int32_t number) {
    if ((unsigned)kind >= STYLE_KIND_COUNT) {
        return STYLE_PROP_UNKNOWN;
    }
    ensure_resolved();
    const int el = part == STYLE_PART_ICON ? -1 : style_logic_element(kind, part);
    /* a list draws no icon */
    const style_prop_status_t st = set_value(el >= 0 ? &resolved_now[el] : NULL,
                                             kind == STYLE_KIND_LIST ? NULL : &kind_icon[kind], part, prop, string, number);
    if (st == STYLE_PROP_OK) {
        touched |= STYLE_CHANGED_KIND(kind);
        dirty |= STYLE_CHANGED_KIND(kind);
    }
    return st;
}

style_prop_status_t ui_style_set_string(const uint8_t ref, const style_part_t part, const char *prop,
                                        const char *value) {
    return value != NULL && prop != NULL ? table_set(ref, part, prop, value, 0) : STYLE_PROP_BAD_VALUE;
}

style_prop_status_t ui_style_set_number(const uint8_t ref, const style_part_t part, const char *prop,
                                        const int32_t value) {
    return prop != NULL ? table_set(ref, part, prop, NULL, value) : STYLE_PROP_UNKNOWN;
}

style_prop_status_t ui_style_kind_set_string(const style_kind_t kind, const style_part_t part, const char *prop,
                                             const char *value) {
    return value != NULL && prop != NULL ? kind_set(kind, part, prop, value, 0) : STYLE_PROP_BAD_VALUE;
}

style_prop_status_t ui_style_kind_set_number(const style_kind_t kind, const style_part_t part, const char *prop,
                                             const int32_t value) {
    return prop != NULL ? kind_set(kind, part, prop, NULL, value) : STYLE_PROP_UNKNOWN;
}

void ui_style_restore(void) {
    if (config_copy == NULL) {
        return;
    }
    const style_props_t *props = (const style_props_t *)config_copy;
    const style_props_t *kind_props = copy_kind_props();
    const uint32_t *icons = copy_icons();
    for (size_t i = 0; i < head_count; i++) {
        if (!(touched & (1u << i))) continue;
        heads[i].icon = icons[i];
        for (int p = 0; p < STYLE_PART_COUNT; p++) {
            const uint8_t e = heads[i].part[p];
            if (e != 0) entries[e - 1].props = props[e - 1];
        }
    }
    for (int k = 0; k < STYLE_KIND_COUNT; k++) {
        if (!(touched & STYLE_CHANGED_KIND(k))) continue;
        kind_icon[k] = icons[head_count + k];
        for (int p = 0; p < STYLE_PART_COUNT; p++) {
            const int el = style_logic_element((style_kind_t)k, (style_part_t)p);
            if (el >= 0) resolved_now[el] = kind_props[el];
        }
    }
    dirty |= touched;
    touched = 0;
    free(config_copy);
    config_copy = NULL;
}

void ui_style_refresh(void) {
    changed_now = dirty;
    dirty = 0;
    reattach_now = 0;
    if (changed_now == 0) {
        return;
    }
    /* The kinds: the same properties as before with other values, so the
     * styles do not grow in the pool. Before a first ui_style_apply nothing
     * carries them, and ui_style_get fills them from the values of now. */
    for (int k = 0; applied && k < STYLE_KIND_COUNT; k++) {
        if (!(changed_now & STYLE_CHANGED_KIND(k))) continue;
        for (int p = 0; p < STYLE_PART_COUNT; p++) {
            const int el = style_logic_element((style_kind_t)k, (style_part_t)p);
            if (el < 0) continue;
            fill_element((style_element_t)el);
            lv_obj_report_style_change(&styles[el]);
        }
    }
    for (size_t i = 0; i < entry_count; i++) {
        style_entry_t *e = &entries[i];
        /* the style this entry belongs to */
        for (size_t s = 0; s < head_count; s++) {
            if ((changed_now & (1u << s)) && heads[s].part[e->part] == i + 1) {
                /* A script may have added a property, or a restart taken
                 * one away: each property is set or removed, so the block
                 * in the pool is as large as the properties it holds. */
                const uint16_t has = style_logic_lvgl_has(&e->props, (style_part_t)e->part);
                const bool fill = has != 0;
                const bool was = e->filled != ENTRY_EMPTY;
                if (!was && !fill) break;
                fill_style(&e->style, &e->props, has, true);
                if (was) lv_obj_report_style_change(&e->style);
                if (fill != was) {
                    /* its users attach it, or take it off, in this walk */
                    e->filled = fill ? ENTRY_FILLED_NOW : ENTRY_EMPTY;
                    reattach_now |= 1u << s;
                }
                break;
            }
        }
    }
}

void ui_style_refresh_done(void) {
    if (reattach_now != 0) {
        /* every user has been walked: the new blocks are on their objects */
        for (size_t i = 0; i < entry_count; i++) {
            if (entries[i].filled == ENTRY_FILLED_NOW) entries[i].filled = ENTRY_FILLED;
        }
    }
    changed_now = 0;
    reattach_now = 0;
}

bool ui_style_changed(const style_kind_t kind, const style_list_t list) {
    return changed_now != 0 && style_logic_changed(changed_now, kind, list);
}
