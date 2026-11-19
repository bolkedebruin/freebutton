//
// Native tests for src/ui/box_logic.h: the font of a text span, and the
// bottom padding of a display item that goes with the value's size, for
// every size and for both answers to "did one of the item's own styles set
// it". With the lookup of style_logic.h (what the value's size is), as
// ui_box.c puts them together.
//
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "../../src/ui/fonts/font_logic.c"
#include "../../src/ui/style_logic.c"
#include "../../src/ui/text_spans.c"
#include "../../src/ui/box_logic.h"

void setUp() {}
void tearDown() {}

static text_span_t span(const int font, const text_size_kind_t kind, const int size) {
    text_span_t s;
    std::memset(&s, 0, sizeof(s));
    s.font = static_cast<int16_t>(font);
    s.size_kind = kind;
    s.size = static_cast<uint8_t>(size);
    return s;
}

static void expect_span_font(const text_span_t s, const bool title, const int face, const int px) {
    int gotFace = -99;
    int gotPx = -99;
    box_logic_span_font(&s, 3, 20, title, &gotFace, &gotPx);
    TEST_ASSERT_EQUAL_INT(face, gotFace);
    TEST_ASSERT_EQUAL_INT(px, gotPx);
}

void test_span_keeps_the_base_without_escapes() {
    expect_span_font(span(TEXT_FONT_BASE, TEXT_SIZE_BASE, 0), false, 3, 20);
}

void test_span_face() {
    expect_span_font(span(5, TEXT_SIZE_BASE, 0), false, 5, 20);
    expect_span_font(span(0, TEXT_SIZE_BASE, 0), false, 0, 20);
    // \f{} of an unknown font: the item's font.
    expect_span_font(span(TEXT_FONT_UNKNOWN, TEXT_SIZE_BASE, 0), false, 3, 20);
}

void test_span_size_steps() {
    const int px[5] = {14, 24, 32, 48, 72};
    for (int step = 1; step <= 5; step++) {
        expect_span_font(span(TEXT_FONT_BASE, TEXT_SIZE_STEP, step), false, 3, px[step - 1]);
    }
    // On a title (a caption, a top label) step 2 is 18 px.
    expect_span_font(span(TEXT_FONT_BASE, TEXT_SIZE_STEP, 2), true, 3, 18);
    expect_span_font(span(TEXT_FONT_BASE, TEXT_SIZE_STEP, 3), true, 3, 32);
    // \s0 and \s6..\s9 were always 14 px.
    expect_span_font(span(TEXT_FONT_BASE, TEXT_SIZE_STEP, 0), false, 3, 14);
    expect_span_font(span(TEXT_FONT_BASE, TEXT_SIZE_STEP, 6), false, 3, 14);
    expect_span_font(span(TEXT_FONT_BASE, TEXT_SIZE_STEP, 9), true, 3, 14);
}

void test_span_size_px() {
    expect_span_font(span(TEXT_FONT_BASE, TEXT_SIZE_PX, 40), false, 3, 40);
    expect_span_font(span(7, TEXT_SIZE_PX, 6), true, 7, 6);
}

static style_props_t props(const uint16_t has, const int face, const int px) {
    style_props_t p;
    std::memset(&p, 0, sizeof(p));
    p.has = has;
    p.face = static_cast<int16_t>(face);
    p.px = static_cast<uint8_t>(px);
    return p;
}

// Every size, with and without a style of the item's own that sets it.
void test_padding_step_every_size() {
    for (int px = 0; px <= 80; px++) {
        for (int own = 0; own <= 1; own++) {
            int expected;
            if (px < 14 || px > FONT_PX_MAX) {
                expected = 0;                      // too small for a padding of its own (or no size)
            } else if (px == 14) {
                expected = own ? 1 : 0;            // the built-in 14: no padding unless the item says 14
            } else if (px <= 19) {
                expected = 1;                      // 19: 5 from 14 and 5 from 24, the smaller
            } else if (px <= 28) {
                expected = 2;                      // 28: 4 from 24 and 4 from 32, the smaller
            } else if (px <= 40) {
                expected = 3;                      // 40: 8 from 32 and 8 from 48, the smaller
            } else if (px <= 60) {
                expected = 4;                      // 60: 12 from 48 and 12 from 72, the smaller
            } else {
                expected = 5;
            }
            char message[32];
            std::snprintf(message, sizeof(message), "px %d own %d", px, own);
            TEST_ASSERT_EQUAL_INT_MESSAGE(expected, box_logic_padding_step(px, own != 0), message);
        }
    }
}

// The steps themselves, which the old "fontsize": 1..5 gave: a style with
// that size gives the same step, also at 14 px.
void test_padding_step_of_the_old_steps() {
    const int px[5] = {14, 24, 32, 48, 72};
    for (int step = 1; step <= 5; step++) {
        TEST_ASSERT_EQUAL_INT(step, box_logic_padding_step(px[step - 1], true));
    }
    // A size "displayitem" gives (not the item): the same, except 14.
    TEST_ASSERT_EQUAL_INT(0, box_logic_padding_step(14, false));
    TEST_ASSERT_EQUAL_INT(2, box_logic_padding_step(24, false));
    TEST_ASSERT_EQUAL_INT(5, box_logic_padding_step(72, false));
}

static void expect_padding(const int step, const int boxtype, const bool has, const int caption, const int box) {
    int gotCaption = -1;
    int gotBox = -1;
    TEST_ASSERT_EQUAL(has, box_logic_padding(step, boxtype, &gotCaption, &gotBox));
    TEST_ASSERT_EQUAL_INT(caption, gotCaption);
    TEST_ASSERT_EQUAL_INT(box, gotBox);
}

void test_padding_table() {
    expect_padding(0, 0, false, 0, 0);
    expect_padding(6, 1, false, 0, 0);
    expect_padding(1, 0, true, 2, 1);
    expect_padding(1, 1, true, 2, 1);
    expect_padding(2, 0, true, 4, 3);
    expect_padding(3, 0, true, 4, 8);
    expect_padding(3, 1, true, 0, 8);
    expect_padding(4, 0, true, 10, 8);
    expect_padding(4, 2, true, 6, 8);
    expect_padding(5, 0, true, 14, 12);
    expect_padding(5, 1, true, 6, 12);
}

// What ui_box.c does: the value's size through the lookup (the item's
// styles, last first, else "displayitem"), and whether one of the item's
// own styles set it.
static int step_of(const style_props_t* kind, const style_props_t* const* layers, const size_t n) {
    style_props_t r;
    style_logic_lookup(kind, layers, n, &r);
    return box_logic_padding_step(r.px, style_logic_layers_set(layers, n, STYLE_HAS_SIZE));
}

void test_padding_through_the_lookup() {
    const style_props_t builtin = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 0, 14);
    const style_props_t kind24 = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 0, 24);   // "displayitem": {"font-size": 24}
    const style_props_t size14 = props(STYLE_HAS_SIZE, 0, 14);
    const style_props_t size48 = props(STYLE_HAS_SIZE, 0, 48);
    const style_props_t faceOnly = props(STYLE_HAS_FONT, 3, 0);
    const style_props_t colourOnly = props(STYLE_HAS_COLOR, 0, 0);

    // No style: the built-in 14 has no padding; a "displayitem" size has its step.
    TEST_ASSERT_EQUAL_INT(0, step_of(&builtin, nullptr, 0));
    TEST_ASSERT_EQUAL_INT(2, step_of(&kind24, nullptr, 0));

    // A style of the item that sets the size: its step, 14 included.
    const style_props_t* small[] = {&size14};
    TEST_ASSERT_EQUAL_INT(1, step_of(&builtin, small, 1));
    TEST_ASSERT_EQUAL_INT(1, step_of(&kind24, small, 1));
    const style_props_t* big[] = {&size48};
    TEST_ASSERT_EQUAL_INT(4, step_of(&builtin, big, 1));

    // Styles that set no size leave the padding as without them.
    const style_props_t* other[] = {&faceOnly, &colourOnly};
    TEST_ASSERT_EQUAL_INT(0, step_of(&builtin, other, 2));
    TEST_ASSERT_EQUAL_INT(2, step_of(&kind24, other, 2));

    // The last style that sets the size gives it; a style without that
    // block (NULL) is passed over.
    const style_props_t* both[] = {&size48, nullptr, &size14, &faceOnly};
    TEST_ASSERT_EQUAL_INT(1, step_of(&builtin, both, 4));
    const style_props_t* reversed[] = {&size14, &size48};
    TEST_ASSERT_EQUAL_INT(4, step_of(&builtin, reversed, 2));
}

// ---- where a text is placed in its part ----------------------------------------

static int no_font(const char*, size_t, bool, int32_t, void*) { return TEXT_FONT_UNKNOWN; }

// The alignment ui_render_text gives `text` in a part whose text-align is
// `style_align` (0 = no style sets one).
static uint8_t placed(const char* text, const uint8_t style_align) {
    char out[64];
    text_span_t spans[8];
    const text_parse_result_t parsed = text_parse(text, std::strlen(text), out, spans, 8, no_font, nullptr);
    return box_logic_text_align(spans, parsed.span_count, style_align);
}

void test_text_align_markup_and_style_are_the_same() {
    // A text that starts with \ac and the same text with text-align
    // "center" are placed the same; so for left and right.
    TEST_ASSERT_EQUAL(TEXT_ALIGN_CENTER, placed("\\acHello", 0));
    TEST_ASSERT_EQUAL(placed("\\acHello", 0), placed("Hello", STYLE_ALIGN_CENTER));
    TEST_ASSERT_EQUAL(TEXT_ALIGN_LEFT, placed("\\alHello", 0));
    TEST_ASSERT_EQUAL(placed("\\alHello", 0), placed("Hello", STYLE_ALIGN_LEFT));
    TEST_ASSERT_EQUAL(TEXT_ALIGN_RIGHT, placed("\\arHello", 0));
    TEST_ASSERT_EQUAL(placed("\\arHello", 0), placed("Hello", STYLE_ALIGN_RIGHT));
    // The same numbers in both enums: one value is passed on.
    TEST_ASSERT_EQUAL(static_cast<int>(TEXT_ALIGN_LEFT), static_cast<int>(STYLE_ALIGN_LEFT));
    TEST_ASSERT_EQUAL(static_cast<int>(TEXT_ALIGN_CENTER), static_cast<int>(STYLE_ALIGN_CENTER));
    TEST_ASSERT_EQUAL(static_cast<int>(TEXT_ALIGN_RIGHT), static_cast<int>(STYLE_ALIGN_RIGHT));
}

void test_text_align_markup_wins_over_the_style() {
    // Neither: not aligned (a display item part is then as wide as its text).
    TEST_ASSERT_EQUAL(TEXT_ALIGN_KEEP, placed("Hello", 0));
    TEST_ASSERT_EQUAL(TEXT_ALIGN_KEEP, placed("", 0));
    // A \a in the text wins over the style's text-align.
    TEST_ASSERT_EQUAL(TEXT_ALIGN_RIGHT, placed("\\arHello", STYLE_ALIGN_CENTER));
    TEST_ASSERT_EQUAL(TEXT_ALIGN_LEFT, placed("\\alHello", STYLE_ALIGN_RIGHT));
    // The last \a of the text, wherever it is.
    TEST_ASSERT_EQUAL(TEXT_ALIGN_RIGHT, placed("\\acHello \\arworld", 0));
    TEST_ASSERT_EQUAL(TEXT_ALIGN_CENTER, placed("Hello \\cff0000red \\acmore", STYLE_ALIGN_RIGHT));
    // Markup that does not align leaves the style's.
    TEST_ASSERT_EQUAL(STYLE_ALIGN_CENTER, placed("\\cff0000red \\s{20}big", STYLE_ALIGN_CENTER));
    // An empty text has no spans: the style's.
    TEST_ASSERT_EQUAL(STYLE_ALIGN_RIGHT, placed("", STYLE_ALIGN_RIGHT));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_span_keeps_the_base_without_escapes);
    RUN_TEST(test_span_face);
    RUN_TEST(test_span_size_steps);
    RUN_TEST(test_span_size_px);
    RUN_TEST(test_padding_step_every_size);
    RUN_TEST(test_padding_step_of_the_old_steps);
    RUN_TEST(test_padding_table);
    RUN_TEST(test_padding_through_the_lookup);
    RUN_TEST(test_text_align_markup_and_style_are_the_same);
    RUN_TEST(test_text_align_markup_wins_over_the_style);
    return UNITY_END();
}
