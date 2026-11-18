// config.json "style": the cascade of the UI element text styles (see
// style_logic.h). No LVGL, no allocation.
//
// Plain C that also compiles as C++ (the native test #includes this file).

#include "style_logic.h"

#include <string.h>

#include "fonts/font_logic.h"

#define STYLE_HAS_BG (STYLE_HAS_BG_COLOR | STYLE_HAS_BG_OPA)
#define STYLE_HAS_FONT_SIZE (STYLE_HAS_FONT | STYLE_HAS_SIZE)

/* ---- roles and defaults ------------------------------------------------------ */

style_role_t style_logic_role_of(style_element_t el) {
    switch (el) {
        case STYLE_EL_KEY_TOPLABEL:
        case STYLE_EL_DISPLAY_CAPTION:
            return STYLE_ROLE_TITLE;
        default:
            return STYLE_ROLE_TEXT;
    }
}

static style_props_t text_(int face, int px, uint32_t color) {
    style_props_t p;
    memset(&p, 0, sizeof(p));
    p.has = STYLE_HAS_FONT | STYLE_HAS_SIZE | STYLE_HAS_COLOR;
    p.face = (int16_t)face;
    p.px = (uint8_t)px;
    p.color = color;
    return p;
}

void style_logic_defaults(style_props_t out[STYLE_EL_COUNT]) {
    out[STYLE_EL_KEY_LABEL] = text_(FONT_FACE_SYSTEM, 32, 0xFFFFFFu);
    out[STYLE_EL_KEY_TOPLABEL] = text_(FONT_FACE_SYSTEM_BOLD, 18, 0xFFD700u);
    out[STYLE_EL_DISPLAY_VALUE] = text_(FONT_FACE_SYSTEM, 14, 0xFFFFFFu);
    style_props_t *u = &out[STYLE_EL_DISPLAY_UNIT];
    memset(u, 0, sizeof(*u));
    u->has = STYLE_HAS_COLOR;
    u->color = 0x999999u;
    out[STYLE_EL_DISPLAY_CAPTION] = text_(FONT_FACE_SYSTEM_BOLD, 14, 0xFFD700u);
    out[STYLE_EL_LIST] = text_(FONT_FACE_SYSTEM, 32, 0x808080u);
    style_props_t *s = &out[STYLE_EL_LIST_SELECTED];
    memset(s, 0, sizeof(*s));
    s->has = STYLE_HAS_COLOR | STYLE_HAS_BG_COLOR | STYLE_HAS_BG_OPA;
    s->color = 0x000000u;
    s->bg_color = 0xFFD700u;
    s->bg_opa = 255;
}

/* `dst` takes the properties of `src` selected by `mask`. */
static void merge_masked(style_props_t *dst, const style_props_t *src, uint16_t mask) {
    const uint16_t h = (uint16_t)(src->has & mask);
    if (h & STYLE_HAS_FONT) dst->face = src->face;
    if (h & STYLE_HAS_SIZE) dst->px = src->px;
    if (h & STYLE_HAS_COLOR) dst->color = src->color;
    if (h & STYLE_HAS_OPA) dst->opa = src->opa;
    if (h & STYLE_HAS_ALIGN) dst->align = src->align;
    if (h & STYLE_HAS_LETTER_SPACE) dst->letter_space = src->letter_space;
    if (h & STYLE_HAS_LINE_SPACE) dst->line_space = src->line_space;
    if (h & STYLE_HAS_DECOR) dst->decor = src->decor;
    if (h & STYLE_HAS_BG_COLOR) dst->bg_color = src->bg_color;
    if (h & STYLE_HAS_BG_OPA) dst->bg_opa = src->bg_opa;
    dst->has = (uint16_t)(dst->has | h);
}

void style_logic_merge(style_props_t *dst, const style_props_t *src) { merge_masked(dst, src, 0xFFFFu); }

void style_logic_resolve(const style_sheet_t *sheet, style_props_t out[STYLE_EL_COUNT]) {
    style_logic_defaults(out);
    if (sheet) {
        for (int el = 0; el < STYLE_EL_COUNT; el++) {
            if (el == STYLE_EL_LIST_SELECTED) continue;
            /* among the elements bg_* belongs to list.selected only (style_logic_set_*) */
            uint16_t role_mask = (uint16_t)~STYLE_HAS_BG;
            /* the unit's font and size are the value's unless the unit
             * element sets them */
            if (el == STYLE_EL_DISPLAY_UNIT) role_mask = (uint16_t)(role_mask & ~STYLE_HAS_FONT_SIZE);
            merge_masked(&out[el], &sheet->role[style_logic_role_of((style_element_t)el)], role_mask);
            merge_masked(&out[el], &sheet->element[el], (uint16_t)~STYLE_HAS_BG);
        }
    }
    /* list.selected: the resolved list (LV_PART_SELECTED falls back to MAIN
     * for inheritable properties), then its own (defaults <- element) */
    style_props_t own = out[STYLE_EL_LIST_SELECTED];
    if (sheet) style_logic_merge(&own, &sheet->element[STYLE_EL_LIST_SELECTED]);
    style_props_t *sel = &out[STYLE_EL_LIST_SELECTED];
    *sel = out[STYLE_EL_LIST];
    sel->has = (uint16_t)(sel->has & ~STYLE_HAS_BG);
    style_logic_merge(sel, &own);
}

/* ---- sizes ------------------------------------------------------------------ */

int style_logic_item_px(int fontsize, bool title, int fallback_px) {
    if (title && fontsize == 2) return 18;
    return font_logic_px(fontsize, fallback_px);
}

/* ---- parsing ---------------------------------------------------------------- */

static bool eq_(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

int style_logic_element(const char *group, const char *name) {
    if (eq_(group, "key")) {
        if (eq_(name, "label")) return STYLE_EL_KEY_LABEL;
        if (eq_(name, "toplabel")) return STYLE_EL_KEY_TOPLABEL;
    } else if (eq_(group, "display")) {
        if (eq_(name, "value")) return STYLE_EL_DISPLAY_VALUE;
        if (eq_(name, "unit")) return STYLE_EL_DISPLAY_UNIT;
        if (eq_(name, "caption")) return STYLE_EL_DISPLAY_CAPTION;
    } else if (eq_(group, "list")) {
        if (!name) return STYLE_EL_LIST;
        if (eq_(name, "selected")) return STYLE_EL_LIST_SELECTED;
    }
    return -1;
}

int style_logic_role(const char *name) {
    if (eq_(name, "text")) return STYLE_ROLE_TEXT;
    if (eq_(name, "title")) return STYLE_ROLE_TITLE;
    return -1;
}

static int hex_(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool style_logic_parse_color(const char *s, uint32_t *out) {
    if (!s) return false;
    if (*s == '#') s++;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        const int d = hex_(s[i]); /* stops at the NUL: hex_('\0') = -1 */
        if (d < 0) return false;
        v = (v << 4) | (uint32_t)d;
    }
    if (s[6] != '\0') return false;
    if (out) *out = v;
    return true;
}

/* Every property name, with the JSON type its value takes. */
typedef enum { PT_STRING, PT_NUMBER, PT_EITHER } prop_type_t;

typedef struct {
    const char *name;
    uint16_t bit;
    prop_type_t type;
} prop_info_t;

static const prop_info_t props_[] = {
    {"text_font", STYLE_HAS_FONT, PT_EITHER}, /* name or config id */
    {"text_size", STYLE_HAS_SIZE, PT_NUMBER},
    {"text_color", STYLE_HAS_COLOR, PT_STRING},
    {"text_opa", STYLE_HAS_OPA, PT_NUMBER},
    {"text_align", STYLE_HAS_ALIGN, PT_STRING},
    {"text_letter_space", STYLE_HAS_LETTER_SPACE, PT_NUMBER},
    {"text_line_space", STYLE_HAS_LINE_SPACE, PT_NUMBER},
    {"text_decor", STYLE_HAS_DECOR, PT_STRING},
    {"bg_color", STYLE_HAS_BG_COLOR, PT_STRING},
    {"bg_opa", STYLE_HAS_BG_OPA, PT_NUMBER},
};

/* The property `prop` names, or NULL when unknown or (bg_*) not allowed. */
static const prop_info_t *prop_(const char *prop, bool allow_bg) {
    if (!prop) return NULL;
    for (size_t i = 0; i < sizeof(props_) / sizeof(props_[0]); i++) {
        if (strcmp(props_[i].name, prop) != 0) continue;
        if ((props_[i].bit & STYLE_HAS_BG) && !allow_bg) return NULL;
        return &props_[i];
    }
    return NULL;
}

static style_prop_status_t set_face_(style_props_t *p, int face) {
    if (face < 0) return STYLE_PROP_UNKNOWN_FONT;
    p->face = (int16_t)face;
    p->has |= STYLE_HAS_FONT;
    return STYLE_PROP_OK;
}

style_prop_status_t style_logic_set_string(style_props_t *p, const char *prop, const char *value, bool allow_bg,
                                           style_font_resolver_t resolve, void *ctx) {
    const prop_info_t *info = prop_(prop, allow_bg);
    if (!info) return STYLE_PROP_UNKNOWN;
    if (info->type == PT_NUMBER || !value) return STYLE_PROP_BAD_VALUE;
    switch (info->bit) {
        case STYLE_HAS_FONT: {
            const size_t len = strlen(value);
            if (len == 0) return STYLE_PROP_BAD_VALUE;
            return set_face_(p, resolve ? resolve(value, len, false, 0, ctx) : -1);
        }
        case STYLE_HAS_COLOR:
        case STYLE_HAS_BG_COLOR: {
            uint32_t c;
            if (!style_logic_parse_color(value, &c)) return STYLE_PROP_BAD_VALUE;
            if (info->bit == STYLE_HAS_COLOR) {
                p->color = c;
            } else {
                p->bg_color = c;
            }
            break;
        }
        case STYLE_HAS_ALIGN: {
            uint8_t a;
            if (strcmp(value, "left") == 0) {
                a = STYLE_ALIGN_LEFT;
            } else if (strcmp(value, "center") == 0) {
                a = STYLE_ALIGN_CENTER;
            } else if (strcmp(value, "right") == 0) {
                a = STYLE_ALIGN_RIGHT;
            } else {
                return STYLE_PROP_BAD_VALUE;
            }
            p->align = a;
            break;
        }
        case STYLE_HAS_DECOR: {
            uint8_t d;
            if (strcmp(value, "none") == 0) {
                d = STYLE_DECOR_NONE;
            } else if (strcmp(value, "underline") == 0) {
                d = STYLE_DECOR_UNDERLINE;
            } else if (strcmp(value, "strikethrough") == 0) {
                d = STYLE_DECOR_STRIKETHROUGH;
            } else if (strcmp(value, "underline strikethrough") == 0) {
                d = STYLE_DECOR_UNDERLINE | STYLE_DECOR_STRIKETHROUGH;
            } else {
                return STYLE_PROP_BAD_VALUE;
            }
            p->decor = d;
            break;
        }
        default:
            return STYLE_PROP_BAD_VALUE;
    }
    p->has |= info->bit;
    return STYLE_PROP_OK;
}

style_prop_status_t style_logic_set_number(style_props_t *p, const char *prop, int32_t value, bool allow_bg,
                                           style_font_resolver_t resolve, void *ctx) {
    const prop_info_t *info = prop_(prop, allow_bg);
    if (!info) return STYLE_PROP_UNKNOWN;
    if (info->type == PT_STRING) return STYLE_PROP_BAD_VALUE;
    switch (info->bit) {
        case STYLE_HAS_FONT:
            if (value < 1) return STYLE_PROP_UNKNOWN_FONT; /* ids start at 1 */
            return set_face_(p, resolve ? resolve(NULL, 0, true, value, ctx) : -1);
        case STYLE_HAS_SIZE:
            if (value < FONT_PX_MIN || value > FONT_PX_MAX) return STYLE_PROP_BAD_VALUE;
            p->px = (uint8_t)value;
            break;
        case STYLE_HAS_OPA:
        case STYLE_HAS_BG_OPA:
            if (value < 0 || value > 255) return STYLE_PROP_BAD_VALUE;
            if (info->bit == STYLE_HAS_OPA) {
                p->opa = (uint8_t)value;
            } else {
                p->bg_opa = (uint8_t)value;
            }
            break;
        case STYLE_HAS_LETTER_SPACE:
        case STYLE_HAS_LINE_SPACE:
            if (value < -100 || value > 100) return STYLE_PROP_BAD_VALUE;
            if (info->bit == STYLE_HAS_LETTER_SPACE) {
                p->letter_space = (int16_t)value;
            } else {
                p->line_space = (int16_t)value;
            }
            break;
        default:
            return STYLE_PROP_BAD_VALUE;
    }
    p->has |= info->bit;
    return STYLE_PROP_OK;
}

/* ---- named styles ------------------------------------------------------------ */

bool style_logic_name_valid(const char *name, const size_t len) {
    /* the rule of every config name (docs/NAMES.md) */
    return len <= STYLE_NAME_MAX && font_logic_valid_name(name, len);
}

style_named_status_t style_logic_named_check(const char *name, const size_t len, const size_t count) {
    if (!style_logic_name_valid(name, len)) return STYLE_NAMED_BAD_NAME;
    if (count >= STYLE_NAMED_MAX) return STYLE_NAMED_FULL;
    return STYLE_NAMED_OK;
}

size_t style_logic_names_add(char *block, size_t used, const char *name, const size_t len) {
    block[used++] = (char)(uint8_t)len;
    memcpy(block + used, name, len);
    return used + len;
}

int style_logic_names_find(const char *block, const size_t count, const char *name, const size_t len) {
    if (!block || !name) return -1;
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        const size_t n = (uint8_t)block[at];
        if (n == len && memcmp(block + at + 1, name, len) == 0) return (int)i;
        at += 1 + n;
    }
    return -1;
}

const char *style_logic_names_get(const char *block, const size_t count, const size_t index, size_t *len) {
    if (!block || index >= count) return NULL;
    size_t at = 0;
    for (size_t i = 0; i < index; i++) at += 1 + (uint8_t)block[at];
    if (len) *len = (uint8_t)block[at];
    return block + at + 1;
}

uint8_t style_logic_named_ref(const char *block, const size_t count, const uint16_t dropped, const char *name,
                              const size_t len) {
    const int i = style_logic_names_find(block, count, name, len);
    if (i < 0 || i >= STYLE_NAMED_MAX || (dropped & (1u << i))) return 0;
    return (uint8_t)(i + 1);
}

size_t style_logic_named_usable(const size_t count, const uint16_t dropped) {
    size_t n = 0;
    for (size_t i = 0; i < count && i < STYLE_NAMED_MAX; i++) {
        if (!(dropped & (1u << i))) n++;
    }
    return n;
}

bool style_logic_named_fits(const style_props_t *named, const style_element_t el) {
    if (el == STYLE_EL_LIST || el == STYLE_EL_LIST_SELECTED) return true;
    return !named || (named->has & STYLE_HAS_BG) == 0;
}

void style_logic_font_layer(const style_props_t *element, const style_props_t *named, style_props_t *out) {
    *out = *element;
    if (named) merge_masked(out, named, STYLE_HAS_FONT_SIZE);
}

bool style_logic_font_local(const int item_face, const int item_fontsize, const style_props_t *named) {
    if (item_face != FONT_FACE_NONE || item_fontsize != 0) return true;
    if (!named) return false;
    const uint16_t fs = (uint16_t)(named->has & STYLE_HAS_FONT_SIZE);
    return fs == STYLE_HAS_FONT || fs == STYLE_HAS_SIZE;
}

bool style_logic_selected_own_font(const style_props_t *list, const style_props_t *selected) {
    return (selected->has & STYLE_HAS_FONT_SIZE) == STYLE_HAS_FONT_SIZE &&
           (selected->face != list->face || selected->px != list->px);
}

bool style_logic_list_selected_font(const style_props_t *list, const style_props_t *selected,
                                    const style_props_t *focus, const int rows_face, const int rows_px, int *face,
                                    int *px) {
    const bool own = style_logic_selected_own_font(list, selected);
    *face = own ? selected->face : rows_face;
    *px = own ? selected->px : rows_px;
    const uint16_t fs = focus ? (uint16_t)(focus->has & STYLE_HAS_FONT_SIZE) : 0;
    if (fs & STYLE_HAS_FONT) *face = focus->face;
    if (fs & STYLE_HAS_SIZE) *px = focus->px;
    return fs == STYLE_HAS_FONT || fs == STYLE_HAS_SIZE;
}

/* What a script may change: STYLE_PROP_OK when `prop` is a property `p`
 * sets and it is not the font or the size. */
static style_prop_status_t named_settable_(const style_props_t *p, const char *prop) {
    const prop_info_t *info = prop_(prop, true);
    if (!info) return STYLE_PROP_UNKNOWN;
    if (info->bit & STYLE_HAS_FONT_SIZE) return STYLE_PROP_FIXED;
    if (!(p->has & info->bit)) return STYLE_PROP_NOT_IN_STYLE;
    return STYLE_PROP_OK;
}

style_prop_status_t style_logic_named_set_string(style_props_t *p, const char *prop, const char *value) {
    const style_prop_status_t st = named_settable_(p, prop);
    return st != STYLE_PROP_OK ? st : style_logic_set_string(p, prop, value, true, NULL, NULL);
}

style_prop_status_t style_logic_named_set_number(style_props_t *p, const char *prop, const int32_t value) {
    const style_prop_status_t st = named_settable_(p, prop);
    return st != STYLE_PROP_OK ? st : style_logic_set_number(p, prop, value, true, NULL, NULL);
}
