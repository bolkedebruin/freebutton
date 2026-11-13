// Display text markup -> spans. Plain C, no LVGL: built by the native tests
// too. The web IDE's preview implements the same grammar (keep in sync:
// web/src/js/text-spans.ts).
//
// Grammar (a backslash starts an escape; anything malformed is kept as
// literal text, backslash included):
//   \\            a literal backslash
//   \f{name}      font for the following text: a config font name or a
//   \f{12}        config font id (digits); unknown -> no change (the
//                 renderer warns once). \f{} = back to the item's font.
//                 Between the braces only [A-Za-z0-9_]* (else literal).
//   \s{40}        size in px: 1..3 decimal digits, FONT_PX_MIN..FONT_PX_MAX
//                 (else literal)
//   \s1 .. \s5    legacy size step (14/24/32/48/72 px); \s0 = back to the
//                 item's size; \s6 .. \s9 = step 6..9 (rendered as 14 px,
//                 as before)
//   \cRRGGBB      colour (6 hex digits)
//   \al \ac \ar   text alignment (left, centre, right) within the item's
//                 width (main display) or the key display
//   \vt \vm \vb   vertical alignment (top, middle, bottom)
//   \uXXXX        the codepoint as UTF-8 (exactly 4 hex digits)
//   \u{X...}      the codepoint as UTF-8 (1..6 hex digits, <= 0x10FFFF, not
//                 a surrogate); e.g. \u{F0D43} for an MDI icon. For both
//                 forms U+0000 and surrogates are literal.
//   \i{name}      image reference (1..63 bytes, no '}'): dropped (not drawn)
//
// The output is the text with the escapes removed (UTF-8, never longer than
// the input) plus one span per run of equal attributes. A span's attributes
// hold until the next escape changes them.
#ifndef TEXT_SPANS_H
#define TEXT_SPANS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Size of a span: inherit the item's, a legacy step, or pixels. */
typedef enum { TEXT_SIZE_BASE = 0, TEXT_SIZE_STEP, TEXT_SIZE_PX } text_size_kind_t;

typedef enum { TEXT_ALIGN_KEEP = 0, TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT } text_align_kind_t;
typedef enum { TEXT_VALIGN_KEEP = 0, TEXT_VALIGN_TOP, TEXT_VALIGN_MIDDLE, TEXT_VALIGN_BOTTOM } text_valign_kind_t;

#define TEXT_FONT_BASE (-1)    /* the item's font */
#define TEXT_FONT_UNKNOWN (-2) /* \f{x} named a font that does not exist */

typedef struct {
    uint16_t offset;      /* into the output text, bytes */
    uint16_t len;         /* bytes; never 0 */
    int16_t font;         /* face index (fonts.h), TEXT_FONT_BASE or TEXT_FONT_UNKNOWN */
    uint8_t size_kind;    /* text_size_kind_t */
    uint8_t size;         /* step (1..9) or px, per size_kind */
    uint8_t has_color;    /* 0: the item's colour */
    uint8_t align;        /* text_align_kind_t */
    uint8_t valign;       /* text_valign_kind_t */
    uint32_t color;       /* 0xRRGGBB when has_color */
} text_span_t;

/* Resolves \f{...}: `name` is `len` bytes (not NUL-terminated); `is_id` when
 * it is all digits (then `id` holds the number). Returns a face index >= 0
 * or TEXT_FONT_UNKNOWN. NULL resolver: every \f{} is TEXT_FONT_UNKNOWN. */
typedef int (*text_font_resolver_t)(const char *name, size_t len, bool is_id, int32_t id, void *ctx);

typedef struct {
    size_t text_len;   /* bytes written to out_text (without the NUL) */
    size_t span_count; /* spans written */
    bool truncated;    /* more spans were needed than max_spans: the last span
                          ran to the end of the text with its attributes */
} text_parse_result_t;

/* Parse `len` bytes of `in` (at most 65535). out_text needs len + 1 bytes
 * (it is NUL-terminated); spans may be NULL to count only (span_count is
 * then the number of spans needed, truncated false). */
text_parse_result_t text_parse(const char *in, size_t len, char *out_text, text_span_t *spans, size_t max_spans,
                               text_font_resolver_t resolve, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
