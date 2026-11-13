// The pure rules of the font registry: see font_logic.h.
//
// Plain C that also compiles as C++ (the native test #includes this file).

#include "font_logic.h"

#include <string.h>

/* ---- validation ------------------------------------------------------------ */

static bool is_alpha_(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool font_logic_valid_name(const char *name, size_t len) {
    if (!name || len == 0 || len > FONT_NAME_MAX) return false;
    if (!is_alpha_(name[0])) return false;
    for (size_t i = 1; i < len; i++) {
        if (!is_alpha_(name[i]) && !is_digit(name[i])) return false;
    }
    return true;
}

bool font_logic_valid_src(const char *src) {
    if (!src) return false;
    size_t n = 0;
    for (; src[n]; n++) {
        if (n >= FONT_SRC_MAX) return false;
        const char c = src[n];
        if (!is_alpha_(c) && !is_digit(c) && c != '.' && c != '-') return false;
    }
    if (n < 5) return false; /* "x.ttf" at least */
    return strcmp(src + n - 4, ".ttf") == 0 || strcmp(src + n - 4, ".otf") == 0;
}

bool font_logic_valid_lineheight(uint16_t lineheight_x100) {
    return lineheight_x100 == 0 ||
           (lineheight_x100 >= FONT_LINEHEIGHT_MIN && lineheight_x100 <= FONT_LINEHEIGHT_MAX);
}

const char *font_logic_warn_str(font_warn_t code) {
    switch (code) {
        case FONT_WARN_BAD_NAME: return "invalid name (1-24 of A-Z a-z 0-9 _, not starting with a digit), skipped";
        case FONT_WARN_BAD_ID: return "invalid id (must be >= 1), skipped";
        case FONT_WARN_DUP_NAME: return "duplicate name, skipped";
        case FONT_WARN_DUP_ID: return "duplicate id, skipped";
        case FONT_WARN_BAD_SRC: return "invalid src (a key like name.ttf / name.otf in bucket fonts), skipped";
        case FONT_WARN_BAD_LINEHEIGHT: return "lineheight out of range (0.8-2.0 em), ignored";
        case FONT_WARN_TOO_MANY: return "too many fonts, skipped";
        case FONT_WARN_UNKNOWN_FALLBACK: return "unknown fallback font, no fallback";
        case FONT_WARN_CYCLE: return "fallback chain loops, link cut";
        case FONT_WARN_TOO_DEEP: return "fallback chain longer than 4 fonts, link cut";
        case FONT_WARN_SYSTEM_SRC: return "system font: src ignored (only fallback and lineheight can be set)";
        default: return "?";
    }
}

/* ---- the face table -------------------------------------------------------- */

static const char *const builtin_name[FONT_FACE_BUILTIN_COUNT] = {FONT_SYSTEM_NAME, FONT_SYSTEM_BOLD_NAME};
static const char *const builtin_src[FONT_FACE_BUILTIN_COUNT] = {FONT_SYSTEM_SRC, FONT_SYSTEM_BOLD_SRC};

size_t font_logic_builtins(font_face_spec_t *faces) {
    for (int f = 0; f < FONT_FACE_BUILTIN_COUNT; f++) {
        font_face_spec_t *s = &faces[f];
        memset(s, 0, sizeof(*s));
        s->name = builtin_name[f];
        s->src = builtin_src[f];
        s->fallback_kind = FONT_FALLBACK_DEFAULT;
        s->fallback_name = "";
        s->fallback = FONT_FACE_NONE;
    }
    return FONT_FACE_BUILTIN_COUNT;
}

size_t font_logic_string_cost(const font_def_t *def, bool overrides_builtin) {
    if (overrides_builtin) return 0; /* the system literals; a src is ignored */
    return strlen(def->name) + 1 + strlen(def->src) + 1;
}

int font_logic_find(const font_face_spec_t *faces, size_t count, const char *name, size_t len) {
    if (!name || len == 0) return FONT_FACE_NONE;
    for (size_t f = 0; f < count; f++) {
        const char *n = faces[f].name;
        if (strlen(n) == len && memcmp(n, name, len) == 0) return (int)f;
    }
    return FONT_FACE_NONE;
}

int font_logic_find_id(const font_face_spec_t *faces, size_t count, int32_t id) {
    if (id < 1) return FONT_FACE_NONE;
    for (size_t f = 0; f < count; f++) {
        if (faces[f].id == id) return (int)f;
    }
    return FONT_FACE_NONE;
}

static void warn_(font_warn_fn warn, void *ctx, font_warn_t code, const char *name) {
    if (warn) warn(ctx, code, name);
}

size_t font_logic_merge(font_face_spec_t *faces, size_t *count, size_t max, const font_def_t *defs, size_t n,
                        size_t string_room, int8_t *accepted_face, font_warn_fn warn, void *ctx) {
    bool overridden[FONT_FACE_BUILTIN_COUNT] = {false, false};
    size_t accepted = 0;
    for (size_t i = 0; i < n; i++) {
        const font_def_t *d = &defs[i];
        if (accepted_face) accepted_face[i] = FONT_FACE_NONE;
        const size_t len = strnlen(d->name, sizeof(d->name));
        if (len >= sizeof(d->name) || !font_logic_valid_name(d->name, len)) {
            warn_(warn, ctx, FONT_WARN_BAD_NAME, d->name);
            continue;
        }
        if (d->id < 0) {
            warn_(warn, ctx, FONT_WARN_BAD_ID, d->name);
            continue;
        }
        int face = font_logic_find(faces, *count, d->name, len);
        const bool override = face >= 0 && face < FONT_FACE_BUILTIN_COUNT && !overridden[face];
        if (face >= 0 && !override) {
            warn_(warn, ctx, FONT_WARN_DUP_NAME, d->name);
            continue;
        }
        if (d->id > 0) {
            const int other = font_logic_find_id(faces, *count, d->id);
            if (other >= 0 && other != face) {
                warn_(warn, ctx, FONT_WARN_DUP_ID, d->name);
                continue;
            }
        }
        if (!override &&
            (strnlen(d->src, sizeof(d->src)) >= sizeof(d->src) || !font_logic_valid_src(d->src))) {
            warn_(warn, ctx, FONT_WARN_BAD_SRC, d->name);
            continue;
        }
        const size_t cost = font_logic_string_cost(d, override);
        if ((!override && *count >= max) || cost > string_room) {
            warn_(warn, ctx, FONT_WARN_TOO_MANY, d->name);
            continue;
        }
        uint16_t lh = d->lineheight_x100;
        if (!font_logic_valid_lineheight(lh)) {
            warn_(warn, ctx, FONT_WARN_BAD_LINEHEIGHT, d->name);
            lh = 0;
        }
        font_face_spec_t *s;
        if (override) {
            /* a system face keeps its file */
            if (d->src[0]) warn_(warn, ctx, FONT_WARN_SYSTEM_SRC, d->name);
            overridden[face] = true;
            s = &faces[face];
        } else {
            face = (int)(*count)++;
            s = &faces[face];
            memset(s, 0, sizeof(*s));
            s->name = d->name;
            s->src = d->src;
        }
        s->id = d->id;
        s->lineheight_x100 = lh;
        s->fallback_kind = d->fallback_kind;
        s->fallback_name = d->fallback_name;
        s->fallback_id = d->fallback_id;
        s->fallback = FONT_FACE_NONE;
        string_room -= cost;
        if (accepted_face) accepted_face[i] = (int8_t)face;
        accepted++;
    }
    return accepted;
}

void font_logic_resolve(font_face_spec_t *faces, size_t count, font_warn_fn warn, void *ctx) {
    /* 1. each reference -> a face index */
    for (size_t f = 0; f < count; f++) {
        font_face_spec_t *s = &faces[f];
        int fb = FONT_FACE_NONE;
        switch (s->fallback_kind) {
            case FONT_FALLBACK_DEFAULT: /* explicit fallbacks only */
            case FONT_FALLBACK_NONE:
                break;
            case FONT_FALLBACK_NAME: {
                const char *n = s->fallback_name ? s->fallback_name : "";
                fb = font_logic_find(faces, count, n, strnlen(n, FONT_NAME_MAX + 1));
                if (fb < 0) warn_(warn, ctx, FONT_WARN_UNKNOWN_FALLBACK, s->name);
                break;
            }
            case FONT_FALLBACK_ID:
                fb = font_logic_find_id(faces, count, s->fallback_id);
                if (fb < 0) warn_(warn, ctx, FONT_WARN_UNKNOWN_FALLBACK, s->name);
                break;
            default:
                warn_(warn, ctx, FONT_WARN_UNKNOWN_FALLBACK, s->name);
                break;
        }
        s->fallback = (int8_t)fb;
    }
    /* 2. walk every chain; cut the link that loops or goes too deep. A cut
     * only removes links, so chains already checked stay valid. */
    for (size_t f = 0; f < count; f++) {
        int chain[FONT_CHAIN_MAX];
        int depth = 1;
        int cur = (int)f;
        chain[0] = cur;
        while (faces[cur].fallback != FONT_FACE_NONE) {
            const int next = faces[cur].fallback;
            bool loop = false;
            for (int k = 0; k < depth; k++) loop = loop || chain[k] == next;
            if (loop) {
                warn_(warn, ctx, FONT_WARN_CYCLE, faces[cur].name);
                faces[cur].fallback = FONT_FACE_NONE;
                break;
            }
            if (depth == FONT_CHAIN_MAX) {
                warn_(warn, ctx, FONT_WARN_TOO_DEEP, faces[cur].name);
                faces[cur].fallback = FONT_FACE_NONE;
                break;
            }
            chain[depth++] = next;
            cur = next;
        }
    }
}

/* ---- sizes ----------------------------------------------------------------- */

static const int legacy_px[5] = {14, 24, 32, 48, 72};

int font_logic_px(int fontsize, int fallback_px) {
    if (fontsize >= 1 && fontsize <= 5) return legacy_px[fontsize - 1];
    if (fontsize >= FONT_PX_MIN && fontsize <= FONT_PX_MAX) return fontsize;
    return fallback_px;
}

/* ---- instance budget ------------------------------------------------------- */

font_budget_t font_logic_budget(size_t used, size_t max, size_t pool_free, size_t pool_biggest) {
    if (used >= max) return FONT_BUDGET_TABLE_FULL;
    if (pool_free < FONT_POOL_RESERVE + FONT_INSTANCE_POOL || pool_biggest < FONT_POOL_BIGGEST)
        return FONT_BUDGET_POOL_LOW;
    return FONT_BUDGET_OK;
}

/* ---- glyph scratch ------------------------------------------------------- */

size_t font_logic_glyph_scratch(uint32_t box_w, uint32_t box_h) {
    return 5120u + (size_t)box_w * (size_t)box_h * 3u / 2u;
}

bool font_logic_glyph_fits(uint32_t box_w, uint32_t box_h, size_t heap_free, size_t heap_biggest) {
    return heap_free >= font_logic_glyph_scratch(box_w, box_h) + FONT_HEAP_RESERVE &&
           heap_biggest >= FONT_GLYPH_BIGGEST;
}

/* ---- line box -------------------------------------------------------------- */

void font_logic_line_box(int px, uint16_t lineheight_x100, bool legacy_medium, int32_t *line_height,
                         int32_t *base_line) {
    if (lineheight_x100) {
        const int32_t lh = (int32_t)((px * (int32_t)lineheight_x100 + 50) / 100);
        const int32_t delta = lh - *line_height;
        /* half of the difference below the baseline; floor for odd negatives */
        int32_t base = *base_line + (delta >= 0 ? delta / 2 : -((-delta + 1) / 2));
        if (base < 0) base = 0;
        if (base > lh) base = lh;
        *line_height = lh;
        *base_line = base;
    } else if (legacy_medium && px >= 48) {
        *line_height = (int32_t)((px * 103 + 50) / 100);
        *base_line = (int32_t)((px * 155 + 500) / 1000);
    }
}
