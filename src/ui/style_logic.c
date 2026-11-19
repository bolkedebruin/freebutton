// config.json "styles": the rules of the looks of buttons, display items
// and lists (see style_logic.h). No LVGL, no allocation, no floating point.
//
// Plain C that also compiles as C++ (the native test #includes this file).

#include "style_logic.h"

#include <string.h>

#include "fonts/font_logic.h"

#define STYLE_HAS_FONT_SIZE (STYLE_HAS_FONT | STYLE_HAS_SIZE)

static bool eq_(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

static bool eqn_(const char *a, size_t len, const char *b) { return strlen(b) == len && memcmp(a, b, len) == 0; }

/* ---- kinds, parts and defaults ------------------------------------------------ */

int style_logic_kind(const char *name, const size_t len) {
    if (!name) return -1;
    if (eqn_(name, len, "button")) return STYLE_KIND_BUTTON;
    if (eqn_(name, len, "displayitem")) return STYLE_KIND_DISPLAYITEM;
    if (eqn_(name, len, "list")) return STYLE_KIND_LIST;
    return -1;
}

int style_logic_part(const char *name) {
    if (eq_(name, "title")) return STYLE_PART_TITLE;
    if (eq_(name, "unit")) return STYLE_PART_UNIT;
    if (eq_(name, "selected")) return STYLE_PART_SELECTED;
    if (eq_(name, "icon")) return STYLE_PART_ICON;
    return -1;
}

int style_logic_element(const style_kind_t kind, const style_part_t part) {
    switch (kind) {
        case STYLE_KIND_BUTTON:
            if (part == STYLE_PART_MAIN) return STYLE_EL_KEY_LABEL;
            if (part == STYLE_PART_TITLE) return STYLE_EL_KEY_TOPLABEL;
            break;
        case STYLE_KIND_DISPLAYITEM:
            if (part == STYLE_PART_MAIN) return STYLE_EL_DISPLAY_VALUE;
            if (part == STYLE_PART_TITLE) return STYLE_EL_DISPLAY_CAPTION;
            if (part == STYLE_PART_UNIT) return STYLE_EL_DISPLAY_UNIT;
            break;
        case STYLE_KIND_LIST:
            if (part == STYLE_PART_MAIN) return STYLE_EL_LIST;
            if (part == STYLE_PART_SELECTED) return STYLE_EL_LIST_SELECTED;
            break;
        default:
            break;
    }
    return -1;
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
    out[STYLE_EL_LIST].has |= STYLE_HAS_LINE_HEIGHT;
    out[STYLE_EL_LIST].line_height = 125; /* a quarter of a line between the rows */
    style_props_t *s = &out[STYLE_EL_LIST_SELECTED];
    memset(s, 0, sizeof(*s));
    s->has = STYLE_HAS_COLOR | STYLE_HAS_BG;
    s->color = 0x000000u;
    s->bg = 0xFFFFD700u;
}

void style_logic_merge(style_props_t *dst, const style_props_t *src) {
    const uint16_t h = src->has;
    if (h & STYLE_HAS_FONT) dst->face = src->face;
    if (h & STYLE_HAS_SIZE) dst->px = src->px;
    if (h & STYLE_HAS_COLOR) dst->color = src->color;
    if (h & STYLE_HAS_OPA) dst->opa = src->opa;
    if (h & STYLE_HAS_ALIGN) dst->align = src->align;
    if (h & STYLE_HAS_LETTER_SPACE) dst->letter_space = src->letter_space;
    if (h & STYLE_HAS_LINE_HEIGHT) dst->line_height = src->line_height;
    if (h & STYLE_HAS_DECOR) dst->decor = src->decor;
    if (h & STYLE_HAS_BG) dst->bg = src->bg;
    dst->has = (uint16_t)(dst->has | h);
}

void style_logic_resolve(const style_sheet_t *sheet, style_props_t out[STYLE_EL_COUNT]) {
    style_logic_defaults(out);
    if (!sheet) return;
    /* Each part by itself: a title, the unit and the selected row take
     * nothing from the kind's main block. */
    for (int el = 0; el < STYLE_EL_COUNT; el++) style_logic_merge(&out[el], &sheet->element[el]);
}

/* ---- sizes ------------------------------------------------------------------ */

int style_logic_item_px(int fontsize, bool title, int fallback_px) {
    if (title && fontsize == 2) return 18;
    return font_logic_px(fontsize, fallback_px);
}

/* ---- parsing ---------------------------------------------------------------- */

static int hex_(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "#" and `digits` hex digits, nothing after them. */
static bool parse_hex_(const char *s, int digits, uint32_t *out) {
    if (!s || *s != '#') return false;
    s++;
    uint32_t v = 0;
    for (int i = 0; i < digits; i++) {
        const int d = hex_(s[i]); /* stops at the NUL: hex_('\0') = -1 */
        if (d < 0) return false;
        v = (v << 4) | (uint32_t)d;
    }
    if (s[digits] != '\0') return false;
    *out = v;
    return true;
}

bool style_logic_parse_color(const char *s, uint32_t *out) {
    uint32_t v;
    if (!parse_hex_(s, 6, &v)) return false;
    if (out) *out = v;
    return true;
}

bool style_logic_parse_background(const char *s, uint32_t *out) {
    uint32_t v;
    if (parse_hex_(s, 6, &v)) {
        v |= 0xFF000000u; /* no alpha given: opaque */
    } else if (parse_hex_(s, 8, &v)) {
        v = (v >> 8) | (v << 24); /* RRGGBBAA -> AARRGGBB */
    } else {
        return false;
    }
    if (out) *out = v;
    return true;
}

/* Every property name, with the JSON type its value takes. */
typedef enum { PT_STRING, PT_INTEGER, PT_DECIMAL, PT_EITHER } prop_type_t;

typedef struct {
    const char *name;
    uint16_t bit;
    prop_type_t type;
} prop_info_t;

static const prop_info_t props_[] = {
    {"color", STYLE_HAS_COLOR, PT_STRING},
    {"font-family", STYLE_HAS_FONT, PT_EITHER}, /* name or config id */
    {"font-size", STYLE_HAS_SIZE, PT_INTEGER},
    {"text-align", STYLE_HAS_ALIGN, PT_STRING},
    {"letter-spacing", STYLE_HAS_LETTER_SPACE, PT_INTEGER},
    {"text-decoration", STYLE_HAS_DECOR, PT_STRING},
    {"opacity", STYLE_HAS_OPA, PT_DECIMAL},
    {"line-height", STYLE_HAS_LINE_HEIGHT, PT_DECIMAL},
    {"background-color", STYLE_HAS_BG, PT_STRING},
};

static const prop_info_t *prop_(const char *prop) {
    if (!prop) return NULL;
    for (size_t i = 0; i < sizeof(props_) / sizeof(props_[0]); i++) {
        if (strcmp(props_[i].name, prop) == 0) return &props_[i];
    }
    return NULL;
}

int style_logic_prop_scale(const char *prop) {
    const prop_info_t *info = prop_(prop);
    if (!info) return 0;
    switch (info->type) {
        case PT_INTEGER:
        case PT_EITHER: return 1;
        case PT_DECIMAL: return 100;
        default: return 0;
    }
}

static style_prop_status_t set_face_(style_props_t *p, int face) {
    if (face < 0) return STYLE_PROP_UNKNOWN_FONT;
    p->face = (int16_t)face;
    p->has |= STYLE_HAS_FONT;
    return STYLE_PROP_OK;
}

style_prop_status_t style_logic_set_string(style_props_t *p, const char *prop, const char *value,
                                           style_font_resolver_t resolve, void *ctx) {
    const prop_info_t *info = prop_(prop);
    if (!info) return STYLE_PROP_UNKNOWN;
    if ((info->type != PT_STRING && info->type != PT_EITHER) || !value) return STYLE_PROP_BAD_VALUE;
    switch (info->bit) {
        case STYLE_HAS_FONT: {
            const size_t len = strlen(value);
            if (len == 0) return STYLE_PROP_BAD_VALUE;
            return set_face_(p, resolve ? resolve(value, len, false, 0, ctx) : -1);
        }
        case STYLE_HAS_COLOR: {
            uint32_t c;
            if (!style_logic_parse_color(value, &c)) return STYLE_PROP_BAD_VALUE;
            p->color = c;
            break;
        }
        case STYLE_HAS_BG: {
            uint32_t c;
            if (!style_logic_parse_background(value, &c)) return STYLE_PROP_BAD_VALUE;
            p->bg = c;
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
            } else if (strcmp(value, "line-through") == 0) {
                d = STYLE_DECOR_LINE_THROUGH;
            } else if (strcmp(value, "underline line-through") == 0 || strcmp(value, "line-through underline") == 0) {
                d = STYLE_DECOR_UNDERLINE | STYLE_DECOR_LINE_THROUGH;
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

style_prop_status_t style_logic_set_number(style_props_t *p, const char *prop, int32_t value,
                                           style_font_resolver_t resolve, void *ctx) {
    const prop_info_t *info = prop_(prop);
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
            if (value < 0 || value > 100) return STYLE_PROP_BAD_VALUE;
            p->opa = (uint8_t)((value * 255 + 50) / 100); /* 0..1 in hundredths -> 0..255, rounded */
            break;
        case STYLE_HAS_LETTER_SPACE:
            if (value < -100 || value > 100) return STYLE_PROP_BAD_VALUE;
            p->letter_space = (int16_t)value;
            break;
        case STYLE_HAS_LINE_HEIGHT:
            if (value < 80 || value > 300) return STYLE_PROP_BAD_VALUE;
            p->line_height = (uint16_t)value;
            break;
        default:
            return STYLE_PROP_BAD_VALUE;
    }
    p->has |= info->bit;
    return STYLE_PROP_OK;
}

style_prop_status_t style_logic_icon_set_string(uint32_t *icon, const char *prop, const char *value) {
    if (!eq_(prop, "color")) return STYLE_PROP_UNKNOWN;
    uint32_t c;
    if (!style_logic_parse_color(value, &c)) return STYLE_PROP_BAD_VALUE;
    *icon = STYLE_ICON_SET | c;
    return STYLE_PROP_OK;
}

/* ---- names -------------------------------------------------------------------- */

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

uint8_t style_logic_named_ref(const char *block, const size_t count, const uint32_t dropped, const char *name,
                              const size_t len) {
    const int i = style_logic_names_find(block, count, name, len);
    if (i < 0 || i >= STYLE_NAMED_MAX || (dropped & (1u << i))) return 0;
    return (uint8_t)(i + 1);
}

size_t style_logic_named_usable(const size_t count, const uint32_t dropped) {
    size_t n = 0;
    for (size_t i = 0; i < count && i < STYLE_NAMED_MAX; i++) {
        if (!(dropped & (1u << i))) n++;
    }
    return n;
}

/* ---- the style list of an item -------------------------------------------------- */

uint8_t style_logic_list_ref(const style_list_t list, const size_t i) {
    return i < STYLE_ITEM_MAX ? (uint8_t)(list >> (8 * i)) : 0;
}

size_t style_logic_list_count(const style_list_t list) {
    size_t n = 0;
    while (n < STYLE_ITEM_MAX && style_logic_list_ref(list, n) != 0) n++;
    return n;
}

bool style_logic_list_next(const char **cursor, const char **name, size_t *len) {
    const char *s = *cursor;
    if (!s) return false;
    while (*s == ' ') s++;
    if (*s == '\0') {
        *cursor = s;
        return false;
    }
    const char *start = s;
    while (*s != '\0' && *s != ' ') s++;
    *name = start;
    *len = (size_t)(s - start);
    *cursor = s;
    return true;
}

void style_logic_list_remove(style_list_t *list, const uint8_t ref) {
    if (ref == 0) return;
    style_list_t out = 0;
    size_t n = 0;
    for (size_t i = 0; i < STYLE_ITEM_MAX; i++) {
        const uint8_t r = style_logic_list_ref(*list, i);
        if (r != 0 && r != ref) out |= (style_list_t)r << (8 * n++);
    }
    *list = out;
}

bool style_logic_list_add(style_list_t *list, const uint8_t ref) {
    if (ref == 0) return true;
    style_list_t next = *list;
    style_logic_list_remove(&next, ref);
    const size_t n = style_logic_list_count(next);
    if (n >= STYLE_ITEM_MAX) return false;
    *list = next | ((style_list_t)ref << (8 * n));
    return true;
}

/* ---- the lookup ----------------------------------------------------------------- */

void style_logic_lookup(const style_props_t *kind, const style_props_t *const *layers, const size_t n,
                        style_props_t *out) {
    *out = *kind;
    for (size_t i = 0; i < n; i++) {
        if (layers[i]) style_logic_merge(out, layers[i]);
    }
}

bool style_logic_layers_set(const style_props_t *const *layers, const size_t n, const uint16_t has_bits) {
    for (size_t i = 0; i < n; i++) {
        if (layers[i] && (layers[i]->has & has_bits)) return true;
    }
    return false;
}

void style_logic_follow_font(const style_props_t *part, const int base_face, const int base_px, int *face, int *px) {
    *face = (part->has & STYLE_HAS_FONT) ? part->face : base_face;
    *px = (part->has & STYLE_HAS_SIZE) ? part->px : base_px;
}

bool style_logic_shared_font(const style_props_t *kind, const style_props_t *const *layers, const size_t n, int *face,
                             int *px) {
    const style_props_t *holder = (kind->has & STYLE_HAS_FONT_SIZE) == STYLE_HAS_FONT_SIZE ? kind : NULL;
    for (size_t i = 0; i < n; i++) {
        if (layers[i] && (layers[i]->has & STYLE_HAS_FONT_SIZE) == STYLE_HAS_FONT_SIZE) holder = layers[i];
    }
    if (!holder) return false;
    *face = holder->face;
    *px = holder->px;
    return true;
}

uint16_t style_logic_lvgl_has(const style_props_t *p, const style_part_t part) {
    uint16_t has = (uint16_t)(p->has & (STYLE_HAS_COLOR | STYLE_HAS_OPA | STYLE_HAS_LETTER_SPACE | STYLE_HAS_DECOR));
    if (part == STYLE_PART_MAIN || part == STYLE_PART_TITLE) has |= (uint16_t)(p->has & STYLE_HAS_ALIGN);
    if (part == STYLE_PART_SELECTED) has |= (uint16_t)(p->has & STYLE_HAS_BG);
    if ((p->has & STYLE_HAS_FONT_SIZE) == STYLE_HAS_FONT_SIZE) has |= STYLE_HAS_FONT_SIZE;
    return has;
}

uint16_t style_logic_span_has(const style_props_t *p, const style_part_t part) {
    return (uint16_t)(style_logic_lvgl_has(p, part) &
                      (STYLE_HAS_OPA | STYLE_HAS_LETTER_SPACE | STYLE_HAS_DECOR | STYLE_HAS_ALIGN));
}

uint8_t style_logic_text_align(const style_props_t *resolved) {
    return (resolved->has & STYLE_HAS_ALIGN) ? resolved->align : 0;
}

void style_logic_selected_row(const style_props_t *selected, const style_props_t *rows, style_props_t *out) {
    *out = *selected;
    if (!(selected->has & STYLE_HAS_OPA) && (rows->has & STYLE_HAS_OPA)) {
        out->opa = rows->opa;
        out->has |= STYLE_HAS_OPA;
    }
    if (!(selected->has & STYLE_HAS_LETTER_SPACE) && (rows->has & STYLE_HAS_LETTER_SPACE)) {
        out->letter_space = rows->letter_space;
        out->has |= STYLE_HAS_LETTER_SPACE;
    }
    if (!(selected->has & STYLE_HAS_DECOR) && (rows->has & STYLE_HAS_DECOR)) {
        out->decor = rows->decor;
        out->has |= STYLE_HAS_DECOR;
    }
}

int style_logic_line_space(const int font_line_height, const int line_height_x100) {
    return font_line_height * (line_height_x100 - 100) / 100;
}

bool style_logic_icon_color(const uint32_t kind_icon, const uint32_t *layers, const size_t n, uint32_t *rgb) {
    uint32_t icon = kind_icon;
    for (size_t i = 0; i < n; i++) {
        if (layers[i] & STYLE_ICON_SET) icon = layers[i];
    }
    if (!(icon & STYLE_ICON_SET)) return false;
    *rgb = icon & 0xFFFFFFu;
    return true;
}

/* ---- a script changes a value --------------------------------------------------- */

const char *style_logic_split_prop(const char *dotted, style_part_t *part) {
    *part = STYLE_PART_MAIN;
    if (!dotted) return dotted;
    const char *dot = strchr(dotted, '.');
    if (!dot) return dotted;
    static const struct {
        const char *name;
        style_part_t part;
    } prefixes[] = {
        {"title", STYLE_PART_TITLE},
        {"unit", STYLE_PART_UNIT},
        {"selected", STYLE_PART_SELECTED},
        {"icon", STYLE_PART_ICON},
    };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        if (eqn_(dotted, (size_t)(dot - dotted), prefixes[i].name)) {
            *part = prefixes[i].part;
            return dot + 1;
        }
    }
    return dotted;
}

style_prop_status_t style_logic_script_settable(const char *prop) {
    const prop_info_t *info = prop_(prop);
    if (!info) return STYLE_PROP_UNKNOWN;
    if (info->bit & (STYLE_HAS_FONT_SIZE | STYLE_HAS_LINE_HEIGHT)) return STYLE_PROP_FIXED;
    return STYLE_PROP_OK;
}

style_prop_status_t style_logic_script_set_string(style_props_t *p, const char *prop, const char *value) {
    const style_prop_status_t st = style_logic_script_settable(prop);
    return st != STYLE_PROP_OK ? st : style_logic_set_string(p, prop, value, NULL, NULL);
}

style_prop_status_t style_logic_script_set_number(style_props_t *p, const char *prop, const int32_t value) {
    const style_prop_status_t st = style_logic_script_settable(prop);
    return st != STYLE_PROP_OK ? st : style_logic_set_number(p, prop, value, NULL, NULL);
}

style_prop_status_t style_logic_script_icon_set_string(uint32_t *icon, const char *prop, const char *value) {
    return style_logic_icon_set_string(icon, prop, value);
}

/* ---- which styles a script changed ----------------------------------------------- */

bool style_logic_changed(const uint32_t changed, const style_kind_t kind, const style_list_t list) {
    if ((unsigned)kind < STYLE_KIND_COUNT && (changed & STYLE_CHANGED_KIND(kind))) return true;
    for (size_t i = 0; i < STYLE_ITEM_MAX; i++) {
        const uint8_t ref = style_logic_list_ref(list, i);
        if (ref >= 1 && ref <= STYLE_NAMED_MAX && (changed & (1u << (ref - 1)))) return true;
    }
    return false;
}

/* ---- keys the config no longer has ---------------------------------------------- */

#define SCOPE_(s) (1u << (s))
#define ITEMS_ (SCOPE_(CONFIG_SCOPE_BUTTON) | SCOPE_(CONFIG_SCOPE_DISPLAYITEM) | SCOPE_(CONFIG_SCOPE_LIST))

static const struct {
    const char *key;
    uint8_t scopes;
    const char *hint;
} removed_[] = {
    {"style", SCOPE_(CONFIG_SCOPE_TOP), "use the top-level \"fonts\" and \"styles\""},
    {"mqttbuttons", SCOPE_(CONFIG_SCOPE_TOP), "use \"buttons\""},
    {"mqttdisplays", SCOPE_(CONFIG_SCOPE_TOP), "use \"displayitems\""},
    {"mqttbrokers", SCOPE_(CONFIG_SCOPE_TOP), "use \"brokers\""},
    {"font", ITEMS_, "use a style with \"font-family\""},
    {"fontsize", ITEMS_, "use a style with \"font-size\""},
    {"topfont", SCOPE_(CONFIG_SCOPE_BUTTON), "use a style with \"title\": {\"font-family\"}"},
    {"topfontsize", SCOPE_(CONFIG_SCOPE_BUTTON), "use a style with \"title\": {\"font-size\"}"},
    {"topstyle", SCOPE_(CONFIG_SCOPE_BUTTON), "use a style with a \"title\" block"},
    {"topLabel", SCOPE_(CONFIG_SCOPE_BUTTON), "use \"toplabel\""},
    {"leds", SCOPE_(CONFIG_SCOPE_BUTTON),
     "put the topics in \"topics\"; the topics under \"leds\" are no longer subscribed"},
    {"buttonuid", SCOPE_(CONFIG_SCOPE_BUTTON), "\"position\" and \"page\" say where the button is"},
    {"id", SCOPE_(CONFIG_SCOPE_BUTTON), "use \"position\""},
    {"labelfont", SCOPE_(CONFIG_SCOPE_DISPLAYITEM), "use a style with \"title\": {\"font-family\"}"},
    {"labelfontsize", SCOPE_(CONFIG_SCOPE_DISPLAYITEM), "use a style with \"title\": {\"font-size\"}"},
    {"labelstyle", SCOPE_(CONFIG_SCOPE_DISPLAYITEM), "use a style with a \"title\" block"},
    {"iconcolor", SCOPE_(CONFIG_SCOPE_DISPLAYITEM), "use a style with \"icon\": {\"color\"}"},
    {"color", SCOPE_(CONFIG_SCOPE_LIST), "use a style with \"color\""},
    {"focuscolor", SCOPE_(CONFIG_SCOPE_LIST), "use a style with \"selected\": {\"background-color\"}"},
    {"focustextcolor", SCOPE_(CONFIG_SCOPE_LIST), "use a style with \"selected\": {\"color\"}"},
    {"focusstyle", SCOPE_(CONFIG_SCOPE_LIST), "use a style with a \"selected\" block"},
    {"autoDiscovery", SCOPE_(CONFIG_SCOPE_BROKER),
     "use \"hassdiscovery\"; Home Assistant discovery is on unless \"hassdiscovery\" is false"},
};

int style_logic_removed_key(const config_scope_t scope, const char *key, const size_t len, const char **hint) {
    if (!key || (unsigned)scope >= CONFIG_SCOPE_COUNT) return -1;
    for (size_t i = 0; i < sizeof(removed_) / sizeof(removed_[0]); i++) {
        if ((removed_[i].scopes & SCOPE_(scope)) && eqn_(key, len, removed_[i].key)) {
            if (hint) *hint = removed_[i].hint;
            return (int)i;
        }
    }
    return -1;
}

#if defined(__cplusplus)
static_assert(sizeof(removed_) / sizeof(removed_[0]) <= CONFIG_REMOVED_KEYS, "one warned bit per removed key");
#else
_Static_assert(sizeof(removed_) / sizeof(removed_[0]) <= CONFIG_REMOVED_KEYS, "one warned bit per removed key");
#endif
