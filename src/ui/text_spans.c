// Display text markup -> spans (text_spans.h). Plain C, no LVGL.

#include "text_spans.h"

#include <string.h>

#define TEXT_PX_MIN 6  /* FONT_PX_MIN (fonts.h, which needs LVGL) */
#define TEXT_PX_MAX 72 /* FONT_PX_MAX */
#define TEXT_IMAGE_NAME_MAX 63

typedef struct {
    int16_t font;
    uint8_t size_kind;
    uint8_t size;
    uint8_t has_color;
    uint8_t align;
    uint8_t valign;
    uint32_t color;
} attrs_t;

typedef struct {
    char *out;
    size_t out_len;
    text_span_t *spans;
    size_t max_spans;
    size_t count;      /* spans written (or needed, when counting) */
    bool counting;     /* spans == NULL: count the spans needed */
    bool truncated;
    bool open;         /* the last span may be extended */
    attrs_t last;      /* attributes of the last span */
} out_t;

static bool attrs_eq(const attrs_t *a, const attrs_t *b) {
    return a->font == b->font && a->size_kind == b->size_kind && a->size == b->size
           && a->has_color == b->has_color && (!a->has_color || a->color == b->color)
           && a->align == b->align && a->valign == b->valign;
}

/* Append n bytes of text with attributes a. */
static void emit(out_t *o, const attrs_t *a, const char *bytes, size_t n) {
    if (n == 0) {
        return;
    }
    memcpy(o->out + o->out_len, bytes, n);
    const size_t offset = o->out_len;
    o->out_len += n;

    if (o->open && (o->truncated || attrs_eq(&o->last, a))) {
        /* same attributes (or no room left): the last span grows */
        if (!o->counting) {
            o->spans[o->count - 1].len = (uint16_t)(o->spans[o->count - 1].len + n);
        }
        return;
    }
    if (o->counting) {
        o->count++;
        o->last = *a;
        o->open = true;
        return;
    }
    if (o->count >= o->max_spans) {
        /* No room for another span: the last one runs on to the end of the
         * text with its own attributes (max_spans 0: the text has none). */
        o->truncated = true;
        if (o->count > 0) {
            o->spans[o->count - 1].len = (uint16_t)(o->spans[o->count - 1].len + n);
        }
        return;
    }
    text_span_t *s = &o->spans[o->count++];
    s->offset = (uint16_t)offset;
    s->len = (uint16_t)n;
    s->font = a->font;
    s->size_kind = a->size_kind;
    s->size = a->size;
    s->has_color = a->has_color;
    s->align = a->align;
    s->valign = a->valign;
    s->color = a->has_color ? a->color : 0;
    o->last = *a;
    o->open = true;
}

static int hex_value(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* n hex digits at p (all present and valid) -> value; -1 otherwise. */
static int32_t hex_n(const char *p, const char *end, size_t n) {
    if ((size_t)(end - p) < n) {
        return -1;
    }
    int32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        const int h = hex_value(p[i]);
        if (h < 0) {
            return -1;
        }
        v = (v << 4) | h;
    }
    return v;
}

/* UTF-8 of cp (1..0x10FFFF, no surrogate) into buf; returns the length. */
static size_t utf8_encode(const uint32_t cp, char *buf) {
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static bool codepoint_ok(const int32_t cp) {
    return cp > 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
}

static bool name_char(const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/*
 * One escape; p points at the character after the backslash (p < end).
 * Applies it to *a (attributes) or emits its text, and returns the input
 * position after it; NULL when the escape is malformed (the caller then keeps
 * the backslash as text and goes on with the character after it).
 */
static const char *escape(out_t *o, attrs_t *a, const char *p, const char *end, text_font_resolver_t resolve,
                          void *ctx) {
    const char c = *p;
    const char *q = p + 1; /* after the escape letter */
    switch (c) {
        case '\\':
            emit(o, a, "\\", 1);
            return q;

        case 'a':
            if (q < end) {
                switch (*q) {
                    case 'l': a->align = TEXT_ALIGN_LEFT; return q + 1;
                    case 'c': a->align = TEXT_ALIGN_CENTER; return q + 1;
                    case 'r': a->align = TEXT_ALIGN_RIGHT; return q + 1;
                    default: break;
                }
            }
            return NULL;

        case 'v':
            if (q < end) {
                switch (*q) {
                    case 't': a->valign = TEXT_VALIGN_TOP; return q + 1;
                    case 'm': a->valign = TEXT_VALIGN_MIDDLE; return q + 1;
                    case 'b': a->valign = TEXT_VALIGN_BOTTOM; return q + 1;
                    default: break;
                }
            }
            return NULL;

        case 'c': {
            const int32_t rgb = hex_n(q, end, 6);
            if (rgb < 0) {
                return NULL;
            }
            a->has_color = 1;
            a->color = (uint32_t)rgb;
            return q + 6;
        }

        case 's':
            if (q < end && *q == '{') {
                /* \s{px}: 1..3 digits, FONT_PX_MIN..FONT_PX_MAX */
                const char *d = q + 1;
                int px = 0;
                size_t digits = 0;
                while (d < end && *d >= '0' && *d <= '9' && digits < 3) {
                    px = px * 10 + (*d - '0');
                    d++;
                    digits++;
                }
                if (digits == 0 || d >= end || *d != '}' || px < TEXT_PX_MIN || px > TEXT_PX_MAX) {
                    return NULL;
                }
                a->size_kind = TEXT_SIZE_PX;
                a->size = (uint8_t)px;
                return d + 1;
            }
            if (q < end && *q >= '0' && *q <= '9') {
                const int step = *q - '0';
                a->size_kind = step == 0 ? TEXT_SIZE_BASE : TEXT_SIZE_STEP;
                a->size = (uint8_t)step;
                return q + 1;
            }
            return NULL;

        case 'f': {
            /* \f{name} / \f{id} / \f{} */
            if (q >= end || *q != '{') {
                return NULL;
            }
            const char *n = q + 1;
            const char *e = n;
            bool digits = true;
            while (e < end && name_char(*e)) {
                digits = digits && *e >= '0' && *e <= '9';
                e++;
            }
            if (e >= end || *e != '}') {
                return NULL;
            }
            const size_t len = (size_t)(e - n);
            if (len == 0) {
                a->font = TEXT_FONT_BASE;
                return e + 1;
            }
            int32_t id = 0;
            if (digits) {
                for (const char *d = n; d < e; d++) {
                    id = id > (INT32_MAX - 9) / 10 ? INT32_MAX : id * 10 + (*d - '0');
                }
            }
            int face = resolve != NULL ? resolve(n, len, digits, digits ? id : 0, ctx) : TEXT_FONT_UNKNOWN;
            if (face < 0) {
                face = TEXT_FONT_UNKNOWN;
            }
            a->font = (int16_t)face;
            return e + 1;
        }

        case 'u': {
            int32_t cp;
            const char *after;
            if (q < end && *q == '{') {
                /* \u{X...}: 1..6 hex digits */
                const char *d = q + 1;
                cp = 0;
                size_t digits = 0;
                while (d < end && digits < 6 && hex_value(*d) >= 0) {
                    cp = (cp << 4) | hex_value(*d);
                    d++;
                    digits++;
                }
                if (digits == 0 || d >= end || *d != '}') {
                    return NULL;
                }
                after = d + 1;
            } else {
                cp = hex_n(q, end, 4);
                after = q + 4;
            }
            if (!codepoint_ok(cp)) {
                return NULL;
            }
            char buf[4];
            emit(o, a, buf, utf8_encode((uint32_t)cp, buf));
            return after;
        }

        case 'i': {
            /* \i{name}: 1..63 bytes up to the closing brace; dropped */
            if (q >= end || *q != '{') {
                return NULL;
            }
            const char *n = q + 1;
            const char *e = n;
            while (e < end && *e != '}' && (size_t)(e - n) <= TEXT_IMAGE_NAME_MAX) {
                e++;
            }
            const size_t len = (size_t)(e - n);
            if (e >= end || *e != '}' || len == 0 || len > TEXT_IMAGE_NAME_MAX) {
                return NULL;
            }
            return e + 1;
        }

        default:
            return NULL;
    }
}

text_parse_result_t text_parse(const char *in, size_t len, char *out_text, text_span_t *spans, size_t max_spans,
                               text_font_resolver_t resolve, void *ctx) {
    text_parse_result_t r = {0, 0, false};
    if (out_text == NULL) {
        return r;
    }
    if (in == NULL) {
        len = 0;
    }
    if (len > UINT16_MAX) {
        len = UINT16_MAX; /* span offsets are 16 bit */
    }

    out_t o;
    memset(&o, 0, sizeof(o));
    o.out = out_text;
    o.spans = spans;
    o.max_spans = spans != NULL ? max_spans : 0;
    o.counting = spans == NULL;

    attrs_t a;
    memset(&a, 0, sizeof(a));
    a.font = TEXT_FONT_BASE;
    a.size_kind = TEXT_SIZE_BASE;

    const char *p = in;
    const char *end = in + len;
    while (p < end) {
        if (*p != '\\') {
            /* a run of plain bytes */
            const char *run = p;
            while (p < end && *p != '\\') {
                p++;
            }
            emit(&o, &a, run, (size_t)(p - run));
            continue;
        }
        if (p + 1 >= end) {
            emit(&o, &a, "\\", 1); /* trailing backslash */
            p++;
            continue;
        }
        const char *next = escape(&o, &a, p + 1, end, resolve, ctx);
        if (next == NULL) {
            emit(&o, &a, "\\", 1); /* malformed: literal, then the rest as text */
            p++;
        } else {
            p = next;
        }
    }

    out_text[o.out_len] = '\0';
    r.text_len = o.out_len;
    r.span_count = o.count;
    r.truncated = o.truncated;
    return r;
}
