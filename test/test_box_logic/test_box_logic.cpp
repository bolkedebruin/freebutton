//
// Native tests for src/ui/box_logic.h: the font of a text span, the base
// fonts of a display item's caption, value and unit, and the bottom padding
// that goes with the value's size. ui_box.c / ui_text.c apply these to LVGL
// objects; the web IDE's emulator applies the same functions.
//
#include <unity.h>

#include <cstring>

#include "../../src/ui/fonts/font_logic.c"
#include "../../src/ui/style_logic.c"
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
    // On a title element step 2 is 18 px, as before "style".
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

void test_base_font_item_over_style() {
    const style_props_t r = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 1, 14);
    TEST_ASSERT_EQUAL_INT(1, box_logic_base_face(FONT_FACE_NONE, &r));
    TEST_ASSERT_EQUAL_INT(4, box_logic_base_face(4, &r));
    TEST_ASSERT_EQUAL_INT(14, box_logic_base_px(0, false, &r));
    TEST_ASSERT_EQUAL_INT(32, box_logic_base_px(3, false, &r));
    TEST_ASSERT_EQUAL_INT(24, box_logic_base_px(2, false, &r));
    TEST_ASSERT_EQUAL_INT(18, box_logic_base_px(2, true, &r));
    TEST_ASSERT_EQUAL_INT(40, box_logic_base_px(40, true, &r));
}

void test_unit_font() {
    const style_props_t v = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 0, 32);
    const style_props_t plain = props(0, 5, 10);  // no font / size of its own: its face and px do not count
    const style_props_t own = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 2, 12);
    const style_props_t ownSize = props(STYLE_HAS_SIZE, 2, 12);

    // Without anything of its own the unit has the value's font and size.
    TEST_ASSERT_EQUAL_INT(0, box_logic_unit_face(FONT_FACE_NONE, &v, &plain));
    TEST_ASSERT_EQUAL_INT(32, box_logic_unit_px(0, &v, &plain));
    // "style" display.unit sets them.
    TEST_ASSERT_EQUAL_INT(2, box_logic_unit_face(FONT_FACE_NONE, &v, &own));
    TEST_ASSERT_EQUAL_INT(12, box_logic_unit_px(0, &v, &own));
    TEST_ASSERT_EQUAL_INT(0, box_logic_unit_face(FONT_FACE_NONE, &v, &ownSize));
    TEST_ASSERT_EQUAL_INT(12, box_logic_unit_px(0, &v, &ownSize));
    // The item's "font" / "fontsize" are the value's and the unit's.
    TEST_ASSERT_EQUAL_INT(6, box_logic_unit_face(6, &v, &own));
    TEST_ASSERT_EQUAL_INT(48, box_logic_unit_px(4, &v, &own));
    TEST_ASSERT_EQUAL_INT(20, box_logic_unit_px(20, &v, &own));
}

void test_padding_step() {
    // A legacy step is itself.
    for (int step = 1; step <= 5; step++) {
        TEST_ASSERT_EQUAL_INT(step, box_logic_padding_step(step, 14));
    }
    // A pixel size: the nearest step's.
    TEST_ASSERT_EQUAL_INT(1, box_logic_padding_step(14, 14));
    TEST_ASSERT_EQUAL_INT(1, box_logic_padding_step(18, 14));
    TEST_ASSERT_EQUAL_INT(2, box_logic_padding_step(20, 14));
    TEST_ASSERT_EQUAL_INT(2, box_logic_padding_step(24, 14));
    TEST_ASSERT_EQUAL_INT(3, box_logic_padding_step(32, 14));
    TEST_ASSERT_EQUAL_INT(4, box_logic_padding_step(50, 14));
    TEST_ASSERT_EQUAL_INT(5, box_logic_padding_step(72, 14));
    // Less than 14 px: none.
    TEST_ASSERT_EQUAL_INT(0, box_logic_padding_step(13, 14));
    TEST_ASSERT_EQUAL_INT(0, box_logic_padding_step(6, 32));
    // No size on the item: the style's px, except the default 14 (none, as before).
    TEST_ASSERT_EQUAL_INT(0, box_logic_padding_step(0, 14));
    TEST_ASSERT_EQUAL_INT(3, box_logic_padding_step(0, 32));
    TEST_ASSERT_EQUAL_INT(0, box_logic_padding_step(0, 10));
    // Not a size at all: the style's px, and (unlike no size) step 1 at 14.
    TEST_ASSERT_EQUAL_INT(1, box_logic_padding_step(200, 14));
    TEST_ASSERT_EQUAL_INT(2, box_logic_padding_step(-1, 24));
}

static void expect_padding(const int step, const int boxtype, const bool has, const int caption, const int box) {
    int c = -1;
    int b = -1;
    TEST_ASSERT_EQUAL(has, box_logic_padding(step, boxtype, &c, &b));
    TEST_ASSERT_EQUAL_INT(caption, c);
    TEST_ASSERT_EQUAL_INT(box, b);
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

// A named style on a display item ("style": value and unit; "labelstyle":
// caption) is the layer between the element style and the item fields
// (style_logic_font_layer), as ui_box.c applies it.
void test_named_style_fonts() {
    const style_props_t value = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 0, 14);
    const style_props_t caption = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 1, 14);
    const style_props_t both = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 5, 48);
    const style_props_t sizeOnly = props(STYLE_HAS_SIZE, 9, 20);
    const style_props_t none = props(0, 9, 99);
    style_props_t r;

    // value: item, else named, else element
    style_logic_font_layer(&value, &both, &r);
    TEST_ASSERT_EQUAL_INT(5, box_logic_base_face(FONT_FACE_NONE, &r));
    TEST_ASSERT_EQUAL_INT(48, box_logic_base_px(0, false, &r));
    TEST_ASSERT_EQUAL_INT(6, box_logic_base_face(6, &r));
    TEST_ASSERT_EQUAL_INT(24, box_logic_base_px(2, false, &r));
    style_logic_font_layer(&value, &sizeOnly, &r);
    TEST_ASSERT_EQUAL_INT(0, box_logic_base_face(FONT_FACE_NONE, &r));
    TEST_ASSERT_EQUAL_INT(20, box_logic_base_px(0, false, &r));
    style_logic_font_layer(&value, &none, &r);
    TEST_ASSERT_EQUAL_INT(0, box_logic_base_face(FONT_FACE_NONE, &r));
    TEST_ASSERT_EQUAL_INT(14, box_logic_base_px(0, false, &r));

    // caption: the title rule stays the item's (step 2 = 18 px)
    style_logic_font_layer(&caption, &both, &r);
    TEST_ASSERT_EQUAL_INT(48, box_logic_base_px(0, true, &r));
    TEST_ASSERT_EQUAL_INT(18, box_logic_base_px(2, true, &r));
}

// The unit: item, else named, else display.unit where it sets one, else the
// value's.
void test_named_style_unit_order() {
    const style_props_t v = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 0, 32);
    const style_props_t unitPlain = props(STYLE_HAS_COLOR, 7, 77);  // no font / size of its own
    const style_props_t unitOwn = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 2, 12);
    const style_props_t both = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 5, 48);
    const style_props_t faceOnly = props(STYLE_HAS_FONT, 5, 99);
    const style_props_t sizeOnly = props(STYLE_HAS_SIZE, 9, 20);
    style_props_t u;

    // no named style: as before
    style_logic_font_layer(&unitPlain, nullptr, &u);
    TEST_ASSERT_EQUAL_INT(0, box_logic_unit_face(FONT_FACE_NONE, &v, &u));
    TEST_ASSERT_EQUAL_INT(32, box_logic_unit_px(0, &v, &u));
    style_logic_font_layer(&unitOwn, nullptr, &u);
    TEST_ASSERT_EQUAL_INT(2, box_logic_unit_face(FONT_FACE_NONE, &v, &u));
    TEST_ASSERT_EQUAL_INT(12, box_logic_unit_px(0, &v, &u));

    // named over display.unit
    style_logic_font_layer(&unitOwn, &both, &u);
    TEST_ASSERT_EQUAL_INT(5, box_logic_unit_face(FONT_FACE_NONE, &v, &u));
    TEST_ASSERT_EQUAL_INT(48, box_logic_unit_px(0, &v, &u));
    // named face only: the size from display.unit where it sets one, else the value's
    style_logic_font_layer(&unitOwn, &faceOnly, &u);
    TEST_ASSERT_EQUAL_INT(5, box_logic_unit_face(FONT_FACE_NONE, &v, &u));
    TEST_ASSERT_EQUAL_INT(12, box_logic_unit_px(0, &v, &u));
    style_logic_font_layer(&unitPlain, &faceOnly, &u);
    TEST_ASSERT_EQUAL_INT(5, box_logic_unit_face(FONT_FACE_NONE, &v, &u));
    TEST_ASSERT_EQUAL_INT(32, box_logic_unit_px(0, &v, &u));
    // named size only: the face from display.unit where it sets one, else the value's
    style_logic_font_layer(&unitPlain, &sizeOnly, &u);
    TEST_ASSERT_EQUAL_INT(0, box_logic_unit_face(FONT_FACE_NONE, &v, &u));
    TEST_ASSERT_EQUAL_INT(20, box_logic_unit_px(0, &v, &u));
    style_logic_font_layer(&unitOwn, &sizeOnly, &u);
    TEST_ASSERT_EQUAL_INT(2, box_logic_unit_face(FONT_FACE_NONE, &v, &u));
    TEST_ASSERT_EQUAL_INT(20, box_logic_unit_px(0, &v, &u));

    // the item over all of it
    style_logic_font_layer(&unitOwn, &both, &u);
    TEST_ASSERT_EQUAL_INT(6, box_logic_unit_face(6, &v, &u));
    TEST_ASSERT_EQUAL_INT(24, box_logic_unit_px(2, &v, &u));
}

// Padding: the named text_size takes the place of display.value's size; an
// item fontsize is unchanged.
void test_named_style_padding() {
    const style_props_t value = props(STYLE_HAS_FONT | STYLE_HAS_SIZE, 0, 14);
    const style_props_t big = props(STYLE_HAS_SIZE, 0, 32);
    const style_props_t same = props(STYLE_HAS_SIZE, 0, 14);
    const style_props_t small = props(STYLE_HAS_SIZE, 0, 10);
    const style_props_t faceOnly = props(STYLE_HAS_FONT, 5, 48);
    style_props_t r;

    style_logic_font_layer(&value, &big, &r);
    TEST_ASSERT_EQUAL_INT(3, box_logic_padding_step(0, r.px));   // the named 32 px: step 3
    TEST_ASSERT_EQUAL_INT(1, box_logic_padding_step(1, r.px));   // the item's step
    TEST_ASSERT_EQUAL_INT(2, box_logic_padding_step(24, r.px));  // the item's px
    style_logic_font_layer(&value, &same, &r);
    TEST_ASSERT_EQUAL_INT(0, box_logic_padding_step(0, r.px));   // 14 px from a style: none, as before
    style_logic_font_layer(&value, &small, &r);
    TEST_ASSERT_EQUAL_INT(0, box_logic_padding_step(0, r.px));   // under 14 px: none
    style_logic_font_layer(&value, &faceOnly, &r);
    TEST_ASSERT_EQUAL_INT(0, box_logic_padding_step(0, r.px));   // no named size: display.value's 14
    style_logic_font_layer(&value, nullptr, &r);
    TEST_ASSERT_EQUAL_INT(0, box_logic_padding_step(0, r.px));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_span_keeps_the_base_without_escapes);
    RUN_TEST(test_span_face);
    RUN_TEST(test_span_size_steps);
    RUN_TEST(test_span_size_px);
    RUN_TEST(test_base_font_item_over_style);
    RUN_TEST(test_unit_font);
    RUN_TEST(test_padding_step);
    RUN_TEST(test_padding_table);
    RUN_TEST(test_named_style_fonts);
    RUN_TEST(test_named_style_unit_order);
    RUN_TEST(test_named_style_padding);
    return UNITY_END();
}
