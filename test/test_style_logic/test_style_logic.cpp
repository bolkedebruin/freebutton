//
// Native tests for config.json "styles" (src/ui/style_logic.c): the
// built-in look and the reserved styles, the property parser, the names,
// an item's style list, the lookup (the item's styles in order, the kind,
// the built-in value, per part, with the unit and selected-row rules),
// fonts, what goes into LVGL, text-align, line-height, the icon colour,
// what a script may set, and the keys a config no longer has. The web
// IDE's style-config.ts follows the same rules (same vectors).
//
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "../../src/ui/fonts/font_logic.c"
#include "../../src/ui/style_logic.c"

void setUp() {}
void tearDown() {}

// ---- helpers ------------------------------------------------------------------

// A face table as fonts.c would have it: the system faces plus "mdi" (id 7).
static int resolver(const char* name, size_t len, bool is_id, int32_t id, void*) {
    if (is_id) return id == 7 ? 2 : -1;
    if (len == 10 && std::memcmp(name, "montserrat", 10) == 0) return FONT_FACE_SYSTEM;
    if (len == 19 && std::memcmp(name, "montserrat_semibold", 19) == 0) return FONT_FACE_SYSTEM_BOLD;
    if (len == 3 && std::memcmp(name, "mdi", 3) == 0) return 2;
    return -1;
}

static style_props_t none() {
    style_props_t p;
    std::memset(&p, 0, sizeof(p));
    return p;
}

static style_sheet_t empty_sheet() {
    style_sheet_t s;
    std::memset(&s, 0, sizeof(s));
    return s;
}

static style_prop_status_t set_s(style_props_t& p, const char* prop, const char* value) {
    return style_logic_set_string(&p, prop, value, resolver, nullptr);
}

static style_prop_status_t set_n(style_props_t& p, const char* prop, int32_t value) {
    return style_logic_set_number(&p, prop, value, resolver, nullptr);
}

static style_props_t colour(uint32_t rgb) {
    style_props_t p = none();
    p.has = STYLE_HAS_COLOR;
    p.color = rgb;
    return p;
}

static style_props_t face(int f) {
    style_props_t p = none();
    p.has = STYLE_HAS_FONT;
    p.face = static_cast<int16_t>(f);
    return p;
}

static style_props_t size(int px) {
    style_props_t p = none();
    p.has = STYLE_HAS_SIZE;
    p.px = static_cast<uint8_t>(px);
    return p;
}

static style_props_t font(int f, int px) {
    style_props_t p = face(f);
    p.has |= STYLE_HAS_SIZE;
    p.px = static_cast<uint8_t>(px);
    return p;
}

// ---- the built-in look and the reserved styles ---------------------------------

void test_defaults() {
    style_props_t d[STYLE_EL_COUNT];
    style_logic_defaults(d);
    const uint16_t text = STYLE_HAS_FONT | STYLE_HAS_SIZE | STYLE_HAS_COLOR;

    TEST_ASSERT_EQUAL_HEX16(text, d[STYLE_EL_KEY_LABEL].has);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, d[STYLE_EL_KEY_LABEL].face);
    TEST_ASSERT_EQUAL(32, d[STYLE_EL_KEY_LABEL].px);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, d[STYLE_EL_KEY_LABEL].color);

    TEST_ASSERT_EQUAL_HEX16(text, d[STYLE_EL_KEY_TOPLABEL].has);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, d[STYLE_EL_KEY_TOPLABEL].face);
    TEST_ASSERT_EQUAL(18, d[STYLE_EL_KEY_TOPLABEL].px);
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, d[STYLE_EL_KEY_TOPLABEL].color);

    TEST_ASSERT_EQUAL_HEX16(text, d[STYLE_EL_DISPLAY_VALUE].has);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, d[STYLE_EL_DISPLAY_VALUE].face);
    TEST_ASSERT_EQUAL(14, d[STYLE_EL_DISPLAY_VALUE].px);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, d[STYLE_EL_DISPLAY_VALUE].color);

    // The unit: a colour only. Its font and size are the value's.
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR, d[STYLE_EL_DISPLAY_UNIT].has);
    TEST_ASSERT_EQUAL_HEX32(0x999999, d[STYLE_EL_DISPLAY_UNIT].color);

    TEST_ASSERT_EQUAL_HEX16(text, d[STYLE_EL_DISPLAY_CAPTION].has);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, d[STYLE_EL_DISPLAY_CAPTION].face);
    TEST_ASSERT_EQUAL(14, d[STYLE_EL_DISPLAY_CAPTION].px);
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, d[STYLE_EL_DISPLAY_CAPTION].color);

    // The list: its rows a quarter line apart.
    TEST_ASSERT_EQUAL_HEX16(text | STYLE_HAS_LINE_HEIGHT, d[STYLE_EL_LIST].has);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, d[STYLE_EL_LIST].face);
    TEST_ASSERT_EQUAL(32, d[STYLE_EL_LIST].px);
    TEST_ASSERT_EQUAL_HEX32(0x808080, d[STYLE_EL_LIST].color);
    TEST_ASSERT_EQUAL(125, d[STYLE_EL_LIST].line_height);

    // The selected row: black on gold, opaque; no font (the rows').
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR | STYLE_HAS_BG, d[STYLE_EL_LIST_SELECTED].has);
    TEST_ASSERT_EQUAL_HEX32(0x000000, d[STYLE_EL_LIST_SELECTED].color);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFD700, d[STYLE_EL_LIST_SELECTED].bg);
}

void test_kinds_parts_and_elements() {
    TEST_ASSERT_EQUAL(STYLE_KIND_BUTTON, style_logic_kind("button", 6));
    TEST_ASSERT_EQUAL(STYLE_KIND_DISPLAYITEM, style_logic_kind("displayitem", 11));
    TEST_ASSERT_EQUAL(STYLE_KIND_LIST, style_logic_kind("list", 4));
    TEST_ASSERT_EQUAL(-1, style_logic_kind("Button", 6));     // case sensitive
    TEST_ASSERT_EQUAL(-1, style_logic_kind("buttons", 7));
    TEST_ASSERT_EQUAL(-1, style_logic_kind("button", 3));     // "but"
    TEST_ASSERT_EQUAL(-1, style_logic_kind("alarm", 5));
    TEST_ASSERT_EQUAL(-1, style_logic_kind(nullptr, 0));

    TEST_ASSERT_EQUAL(STYLE_PART_TITLE, style_logic_part("title"));
    TEST_ASSERT_EQUAL(STYLE_PART_UNIT, style_logic_part("unit"));
    TEST_ASSERT_EQUAL(STYLE_PART_SELECTED, style_logic_part("selected"));
    TEST_ASSERT_EQUAL(STYLE_PART_ICON, style_logic_part("icon"));
    TEST_ASSERT_EQUAL(-1, style_logic_part("color"));
    TEST_ASSERT_EQUAL(-1, style_logic_part("main"));
    TEST_ASSERT_EQUAL(-1, style_logic_part(nullptr));

    TEST_ASSERT_EQUAL(STYLE_EL_KEY_LABEL, style_logic_element(STYLE_KIND_BUTTON, STYLE_PART_MAIN));
    TEST_ASSERT_EQUAL(STYLE_EL_KEY_TOPLABEL, style_logic_element(STYLE_KIND_BUTTON, STYLE_PART_TITLE));
    TEST_ASSERT_EQUAL(STYLE_EL_DISPLAY_VALUE, style_logic_element(STYLE_KIND_DISPLAYITEM, STYLE_PART_MAIN));
    TEST_ASSERT_EQUAL(STYLE_EL_DISPLAY_CAPTION, style_logic_element(STYLE_KIND_DISPLAYITEM, STYLE_PART_TITLE));
    TEST_ASSERT_EQUAL(STYLE_EL_DISPLAY_UNIT, style_logic_element(STYLE_KIND_DISPLAYITEM, STYLE_PART_UNIT));
    TEST_ASSERT_EQUAL(STYLE_EL_LIST, style_logic_element(STYLE_KIND_LIST, STYLE_PART_MAIN));
    TEST_ASSERT_EQUAL(STYLE_EL_LIST_SELECTED, style_logic_element(STYLE_KIND_LIST, STYLE_PART_SELECTED));
    // A sub-block the kind does not have is ignored.
    TEST_ASSERT_EQUAL(-1, style_logic_element(STYLE_KIND_BUTTON, STYLE_PART_UNIT));
    TEST_ASSERT_EQUAL(-1, style_logic_element(STYLE_KIND_BUTTON, STYLE_PART_SELECTED));
    TEST_ASSERT_EQUAL(-1, style_logic_element(STYLE_KIND_DISPLAYITEM, STYLE_PART_SELECTED));
    TEST_ASSERT_EQUAL(-1, style_logic_element(STYLE_KIND_LIST, STYLE_PART_TITLE));
    TEST_ASSERT_EQUAL(-1, style_logic_element(STYLE_KIND_LIST, STYLE_PART_UNIT));
    TEST_ASSERT_EQUAL(-1, style_logic_element(STYLE_KIND_BUTTON, STYLE_PART_ICON));
}

void test_resolve_without_styles_is_the_built_in_look() {
    style_props_t d[STYLE_EL_COUNT];
    style_props_t r[STYLE_EL_COUNT];
    style_logic_defaults(d);
    style_logic_resolve(nullptr, r);
    TEST_ASSERT_EQUAL_MEMORY(d, r, sizeof(d));
    const style_sheet_t sheet = empty_sheet();
    style_logic_resolve(&sheet, r);
    TEST_ASSERT_EQUAL_MEMORY(d, r, sizeof(d));
}

void test_resolve_a_reserved_style_changes_its_part_only() {
    // "button": {"font-size": 40, "color": "#ff0000"}: the label, not the top label.
    style_sheet_t sheet = empty_sheet();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(sheet.element[STYLE_EL_KEY_LABEL], "font-size", 40));
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(sheet.element[STYLE_EL_KEY_LABEL], "color", "#ff0000"));
    style_props_t d[STYLE_EL_COUNT];
    style_props_t r[STYLE_EL_COUNT];
    style_logic_defaults(d);
    style_logic_resolve(&sheet, r);
    TEST_ASSERT_EQUAL(40, r[STYLE_EL_KEY_LABEL].px);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, r[STYLE_EL_KEY_LABEL].face);   // not set: built-in
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, r[STYLE_EL_KEY_LABEL].color);
    // Titles do not inherit from main.
    TEST_ASSERT_EQUAL_MEMORY(&d[STYLE_EL_KEY_TOPLABEL], &r[STYLE_EL_KEY_TOPLABEL], sizeof(style_props_t));
    for (int el = STYLE_EL_DISPLAY_VALUE; el < STYLE_EL_COUNT; el++) {
        TEST_ASSERT_EQUAL_MEMORY(&d[el], &r[el], sizeof(style_props_t));
    }
}

void test_resolve_displayitem_main_does_not_reach_unit_or_caption() {
    // "displayitem": {"font-size": 24, "color": "#00ff00", "font-family": "mdi"}
    style_sheet_t sheet = empty_sheet();
    set_n(sheet.element[STYLE_EL_DISPLAY_VALUE], "font-size", 24);
    set_s(sheet.element[STYLE_EL_DISPLAY_VALUE], "color", "#00ff00");
    set_s(sheet.element[STYLE_EL_DISPLAY_VALUE], "font-family", "mdi");
    style_props_t r[STYLE_EL_COUNT];
    style_logic_resolve(&sheet, r);
    TEST_ASSERT_EQUAL(24, r[STYLE_EL_DISPLAY_VALUE].px);
    // The unit keeps its own colour and still has no font of its own: a
    // main colour does not colour the unit.
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR, r[STYLE_EL_DISPLAY_UNIT].has);
    TEST_ASSERT_EQUAL_HEX32(0x999999, r[STYLE_EL_DISPLAY_UNIT].color);
    TEST_ASSERT_EQUAL(14, r[STYLE_EL_DISPLAY_CAPTION].px);
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, r[STYLE_EL_DISPLAY_CAPTION].color);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, r[STYLE_EL_DISPLAY_CAPTION].face);

    // "displayitem": {"unit": {"font-size": 10}}: the unit's own size.
    sheet = empty_sheet();
    set_n(sheet.element[STYLE_EL_DISPLAY_UNIT], "font-size", 10);
    style_logic_resolve(&sheet, r);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR | STYLE_HAS_SIZE, r[STYLE_EL_DISPLAY_UNIT].has);
    TEST_ASSERT_EQUAL(10, r[STYLE_EL_DISPLAY_UNIT].px);
}

void test_resolve_list_main_does_not_reach_the_selected_row() {
    // "list": {"color": "#112233", "font-size": 24, "selected": {"background-color": "#ffffff"}}
    style_sheet_t sheet = empty_sheet();
    set_s(sheet.element[STYLE_EL_LIST], "color", "#112233");
    set_n(sheet.element[STYLE_EL_LIST], "font-size", 24);
    set_s(sheet.element[STYLE_EL_LIST_SELECTED], "background-color", "#ffffff");
    style_props_t r[STYLE_EL_COUNT];
    style_logic_resolve(&sheet, r);
    TEST_ASSERT_EQUAL_HEX32(0x112233, r[STYLE_EL_LIST].color);
    TEST_ASSERT_EQUAL(24, r[STYLE_EL_LIST].px);
    TEST_ASSERT_EQUAL(125, r[STYLE_EL_LIST].line_height);
    // The selected row: its built-in black text (a main colour never
    // colours it), the new background, and still no font.
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR | STYLE_HAS_BG, r[STYLE_EL_LIST_SELECTED].has);
    TEST_ASSERT_EQUAL_HEX32(0x000000, r[STYLE_EL_LIST_SELECTED].color);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, r[STYLE_EL_LIST_SELECTED].bg);
}

void test_item_px_steps_of_the_markup() {
    TEST_ASSERT_EQUAL(14, style_logic_item_px(1, false, 99));
    TEST_ASSERT_EQUAL(24, style_logic_item_px(2, false, 99));
    TEST_ASSERT_EQUAL(18, style_logic_item_px(2, true, 99));    // a title: step 2 = 18
    TEST_ASSERT_EQUAL(32, style_logic_item_px(3, true, 99));
    TEST_ASSERT_EQUAL(48, style_logic_item_px(4, false, 99));
    TEST_ASSERT_EQUAL(72, style_logic_item_px(5, false, 99));
    TEST_ASSERT_EQUAL(40, style_logic_item_px(40, false, 99));
    TEST_ASSERT_EQUAL(99, style_logic_item_px(0, false, 99));
    TEST_ASSERT_EQUAL(99, style_logic_item_px(73, true, 99));
}

// ---- the property parser ---------------------------------------------------------

void test_color() {
    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "color", "#ff8000"));
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR, p.has);
    TEST_ASSERT_EQUAL_HEX32(0xFF8000, p.color);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "color", "#AbCdEf"));
    TEST_ASSERT_EQUAL_HEX32(0xABCDEF, p.color);
    // "#rrggbb" only.
    style_props_t q = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(q, "color", "ff8000"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(q, "color", "#ff800"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(q, "color", "#ff80000"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(q, "color", "#ff8000ff"));   // alpha: the background only
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(q, "color", "#gg8000"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(q, "color", "red"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(q, "color", ""));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(q, "color", nullptr));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(q, "color", 255));
    TEST_ASSERT_EQUAL_HEX16(0, q.has);   // unchanged on every error
}

void test_parse_color_and_background() {
    uint32_t c = 0;
    TEST_ASSERT_TRUE(style_logic_parse_color("#010203", &c));
    TEST_ASSERT_EQUAL_HEX32(0x010203, c);
    TEST_ASSERT_TRUE(style_logic_parse_color("#010203", nullptr));
    TEST_ASSERT_FALSE(style_logic_parse_color("010203", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color(nullptr, &c));

    // "#rrggbb" is opaque; "#rrggbbaa" has its own opacity.
    TEST_ASSERT_TRUE(style_logic_parse_background("#102030", &c));
    TEST_ASSERT_EQUAL_HEX32(0xFF102030, c);
    TEST_ASSERT_TRUE(style_logic_parse_background("#10203080", &c));
    TEST_ASSERT_EQUAL_HEX32(0x80102030, c);
    TEST_ASSERT_TRUE(style_logic_parse_background("#10203000", &c));
    TEST_ASSERT_EQUAL_HEX32(0x00102030, c);
    TEST_ASSERT_TRUE(style_logic_parse_background("#FFFFFFFF", &c));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, c);
    TEST_ASSERT_FALSE(style_logic_parse_background("#1020308", &c));     // 7 digits
    TEST_ASSERT_FALSE(style_logic_parse_background("#102030800", &c));   // 9 digits
    TEST_ASSERT_FALSE(style_logic_parse_background("10203080", &c));
    TEST_ASSERT_FALSE(style_logic_parse_background("", &c));
    TEST_ASSERT_FALSE(style_logic_parse_background(nullptr, &c));
}

void test_background_color() {
    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "background-color", "#ffd700"));
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_BG, p.has);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFD700, p.bg);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "background-color", "#00c85340"));
    TEST_ASSERT_EQUAL_HEX32(0x4000C853, p.bg);
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "background-color", "#00c8534"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "background-color", 1));
    TEST_ASSERT_EQUAL_HEX32(0x4000C853, p.bg);
}

void test_font_family() {
    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "font-family", "montserrat_semibold"));
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_FONT, p.has);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, p.face);
    // The id of a "fonts" entry.
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "font-family", 7));
    TEST_ASSERT_EQUAL(2, p.face);
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN_FONT, set_s(p, "font-family", "nosuch"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN_FONT, set_n(p, "font-family", 8));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN_FONT, set_n(p, "font-family", 0));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "font-family", ""));
    TEST_ASSERT_EQUAL(2, p.face);
    // Without a resolver no font is known.
    style_props_t q = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN_FONT, style_logic_set_string(&q, "font-family", "mdi", nullptr, nullptr));
    TEST_ASSERT_EQUAL_HEX16(0, q.has);
}

void test_font_size() {
    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "font-size", 6));
    TEST_ASSERT_EQUAL(6, p.px);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "font-size", 72));
    TEST_ASSERT_EQUAL(72, p.px);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_SIZE, p.has);
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "font-size", 5));     // the old steps 1..5 are not sizes
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "font-size", 3));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "font-size", 73));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "font-size", -14));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "font-size", "14"));  // no units, no strings
    TEST_ASSERT_EQUAL(72, p.px);
}

void test_text_align() {
    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "text-align", "left"));
    TEST_ASSERT_EQUAL(STYLE_ALIGN_LEFT, p.align);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "text-align", "center"));
    TEST_ASSERT_EQUAL(STYLE_ALIGN_CENTER, p.align);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "text-align", "right"));
    TEST_ASSERT_EQUAL(STYLE_ALIGN_RIGHT, p.align);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_ALIGN, p.has);
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "text-align", "centre"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "text-align", "justify"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "text-align", 1));
    TEST_ASSERT_EQUAL(STYLE_ALIGN_RIGHT, p.align);
}

void test_letter_spacing() {
    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "letter-spacing", -100));
    TEST_ASSERT_EQUAL(-100, p.letter_space);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "letter-spacing", 100));
    TEST_ASSERT_EQUAL(100, p.letter_space);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "letter-spacing", 0));
    TEST_ASSERT_EQUAL(0, p.letter_space);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_LETTER_SPACE, p.has);
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "letter-spacing", 101));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "letter-spacing", -101));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "letter-spacing", "2"));
}

void test_text_decoration() {
    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "text-decoration", "underline"));
    TEST_ASSERT_EQUAL(STYLE_DECOR_UNDERLINE, p.decor);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "text-decoration", "line-through"));
    TEST_ASSERT_EQUAL(STYLE_DECOR_LINE_THROUGH, p.decor);
    // Both words, in either order.
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "text-decoration", "underline line-through"));
    TEST_ASSERT_EQUAL(STYLE_DECOR_UNDERLINE | STYLE_DECOR_LINE_THROUGH, p.decor);
    p.decor = 0;
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "text-decoration", "line-through underline"));
    TEST_ASSERT_EQUAL(STYLE_DECOR_UNDERLINE | STYLE_DECOR_LINE_THROUGH, p.decor);
    // "none" is a value a style sets (it takes a decoration of the kind away).
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_s(p, "text-decoration", "none"));
    TEST_ASSERT_EQUAL(STYLE_DECOR_NONE, p.decor);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_DECOR, p.has);
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "text-decoration", "strikethrough"));   // the LVGL word
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "text-decoration", "underline underline"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "text-decoration", "underline  line-through"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "text-decoration", "overline"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "text-decoration", 1));
    // The bits are LVGL's lv_text_decor_t.
    TEST_ASSERT_EQUAL(1, STYLE_DECOR_UNDERLINE);
    TEST_ASSERT_EQUAL(2, STYLE_DECOR_LINE_THROUGH);
}

void test_opacity_in_hundredths() {
    // The caller gives 0..1 as 0..100; stored 0..255.
    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "opacity", 0));
    TEST_ASSERT_EQUAL(0, p.opa);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "opacity", 100));
    TEST_ASSERT_EQUAL(255, p.opa);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "opacity", 50));
    TEST_ASSERT_EQUAL(128, p.opa);          // 127.5, rounded
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "opacity", 1));
    TEST_ASSERT_EQUAL(3, p.opa);            // 2.55
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "opacity", 99));
    TEST_ASSERT_EQUAL(252, p.opa);          // 252.45
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_OPA, p.has);
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "opacity", 101));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "opacity", -1));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "opacity", 255));   // the LVGL range is not the config's
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "opacity", "0.5"));
    TEST_ASSERT_EQUAL(252, p.opa);
    // Monotonic over the whole range.
    int last = -1;
    for (int v = 0; v <= 100; v++) {
        TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "opacity", v));
        TEST_ASSERT_TRUE(p.opa >= last);
        last = p.opa;
    }
}

void test_line_height_in_hundredths() {
    // 0.8..3.0 as 80..300, kept as it is.
    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "line-height", 80));
    TEST_ASSERT_EQUAL(80, p.line_height);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "line-height", 300));
    TEST_ASSERT_EQUAL(300, p.line_height);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, set_n(p, "line-height", 125));
    TEST_ASSERT_EQUAL(125, p.line_height);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_LINE_HEIGHT, p.has);
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "line-height", 79));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "line-height", 301));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_n(p, "line-height", 1));    // 0.01
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, set_s(p, "line-height", "1.25"));
    TEST_ASSERT_EQUAL(125, p.line_height);
}

void test_prop_scale() {
    TEST_ASSERT_EQUAL(1, style_logic_prop_scale("font-size"));
    TEST_ASSERT_EQUAL(1, style_logic_prop_scale("letter-spacing"));
    TEST_ASSERT_EQUAL(1, style_logic_prop_scale("font-family"));     // an id
    TEST_ASSERT_EQUAL(100, style_logic_prop_scale("opacity"));
    TEST_ASSERT_EQUAL(100, style_logic_prop_scale("line-height"));
    TEST_ASSERT_EQUAL(0, style_logic_prop_scale("color"));
    TEST_ASSERT_EQUAL(0, style_logic_prop_scale("text-align"));
    TEST_ASSERT_EQUAL(0, style_logic_prop_scale("text-decoration"));
    TEST_ASSERT_EQUAL(0, style_logic_prop_scale("background-color"));
    TEST_ASSERT_EQUAL(0, style_logic_prop_scale("nosuch"));
    TEST_ASSERT_EQUAL(0, style_logic_prop_scale(nullptr));
}

void test_unknown_properties() {
    style_props_t p = none();
    // The names of the version before, and of LVGL: not properties.
    const char* const old[] = {"text_color", "text_font", "text_size", "text_opa", "text_align", "text_letter_space",
                               "text_line_space", "text_decor", "bg_color", "bg_opa"};
    for (const char* name : old) {
        TEST_ASSERT_EQUAL_MESSAGE(STYLE_PROP_UNKNOWN, set_s(p, name, "#ffffff"), name);
        TEST_ASSERT_EQUAL_MESSAGE(STYLE_PROP_UNKNOWN, set_n(p, name, 1), name);
    }
    // A bold face is another font: there is no font-weight or font-style.
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, set_s(p, "font-weight", "bold"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, set_n(p, "font-weight", 700));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, set_s(p, "font-style", "italic"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, set_s(p, "Color", "#ffffff"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, set_s(p, "", "#ffffff"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, set_s(p, nullptr, "#ffffff"));
    // A sub-block name is not a property.
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, set_s(p, "title", "x"));
    TEST_ASSERT_EQUAL_HEX16(0, p.has);
}

void test_icon_block() {
    uint32_t icon = 0;
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_icon_set_string(&icon, "color", "#00ff00"));
    TEST_ASSERT_EQUAL_HEX32(STYLE_ICON_SET | 0x00FF00, icon);
    // Black is a colour too: the flag says it is set.
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_icon_set_string(&icon, "color", "#000000"));
    TEST_ASSERT_EQUAL_HEX32(STYLE_ICON_SET, icon);
    // "color" only.
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_icon_set_string(&icon, "font-size", "#00ff00"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_icon_set_string(&icon, "opacity", "1"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_icon_set_string(&icon, "color", "00ff00"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_icon_set_string(&icon, "color", nullptr));
    TEST_ASSERT_EQUAL_HEX32(STYLE_ICON_SET, icon);
}

// ---- names ------------------------------------------------------------------------

void test_names() {
    TEST_ASSERT_TRUE(style_logic_name_valid("alarm", 5));
    TEST_ASSERT_TRUE(style_logic_name_valid("_a1", 3));
    TEST_ASSERT_TRUE(style_logic_name_valid("A", 1));
    TEST_ASSERT_TRUE(style_logic_name_valid("abcdefghijklmnopqrstuvwx", 24));
    TEST_ASSERT_FALSE(style_logic_name_valid("abcdefghijklmnopqrstuvwxy", 25));
    TEST_ASSERT_FALSE(style_logic_name_valid("", 0));
    TEST_ASSERT_FALSE(style_logic_name_valid("1big", 4));
    TEST_ASSERT_FALSE(style_logic_name_valid("big-text", 8));
    TEST_ASSERT_FALSE(style_logic_name_valid("big text", 8));
    TEST_ASSERT_FALSE(style_logic_name_valid("b\xc3\xa9", 3));
}

void test_the_limit_of_32_styles() {
    // 32 with "button", "displayitem" and "list", which always exist: 29 others.
    TEST_ASSERT_EQUAL(32, STYLE_MAX);
    TEST_ASSERT_EQUAL(29, STYLE_NAMED_MAX);
    TEST_ASSERT_EQUAL(4, STYLE_ITEM_MAX);
    TEST_ASSERT_EQUAL(STYLE_NAMED_OK, style_logic_named_check("alarm", 5, 0));
    TEST_ASSERT_EQUAL(STYLE_NAMED_OK, style_logic_named_check("alarm", 5, 28));
    TEST_ASSERT_EQUAL(STYLE_NAMED_FULL, style_logic_named_check("alarm", 5, 29));
    TEST_ASSERT_EQUAL(STYLE_NAMED_FULL, style_logic_named_check("alarm", 5, 40));
    // A bad name is a bad name, also when the table is full.
    TEST_ASSERT_EQUAL(STYLE_NAMED_BAD_NAME, style_logic_named_check("1x", 2, 0));
    TEST_ASSERT_EQUAL(STYLE_NAMED_BAD_NAME, style_logic_named_check("1x", 2, 29));
}

void test_names_block() {
    char block[64];
    size_t used = 0;
    used = style_logic_names_add(block, used, "alarm", 5);
    TEST_ASSERT_EQUAL(6, used);
    used = style_logic_names_add(block, used, "big", 3);
    used = style_logic_names_add(block, used, "a", 1);
    TEST_ASSERT_EQUAL(12, used);   // each name once: 1 + its length

    TEST_ASSERT_EQUAL(0, style_logic_names_find(block, 3, "alarm", 5));
    TEST_ASSERT_EQUAL(1, style_logic_names_find(block, 3, "big", 3));
    TEST_ASSERT_EQUAL(2, style_logic_names_find(block, 3, "a", 1));
    TEST_ASSERT_EQUAL(-1, style_logic_names_find(block, 3, "alar", 4));
    TEST_ASSERT_EQUAL(-1, style_logic_names_find(block, 3, "Alarm", 5));
    TEST_ASSERT_EQUAL(-1, style_logic_names_find(block, 2, "a", 1));   // not among the first two
    TEST_ASSERT_EQUAL(-1, style_logic_names_find(nullptr, 3, "a", 1));

    size_t len = 0;
    const char* name = style_logic_names_get(block, 3, 1, &len);
    TEST_ASSERT_EQUAL(3, len);
    TEST_ASSERT_EQUAL_MEMORY("big", name, 3);
    TEST_ASSERT_NULL(style_logic_names_get(block, 3, 3, &len));
    TEST_ASSERT_NULL(style_logic_names_get(nullptr, 3, 0, &len));
}

void test_dropped_styles_are_unknown() {
    char block[64];
    size_t used = 0;
    used = style_logic_names_add(block, used, "alarm", 5);
    used = style_logic_names_add(block, used, "big", 3);
    used = style_logic_names_add(block, used, "night", 5);
    TEST_ASSERT_EQUAL(1, style_logic_named_ref(block, 3, 0, "alarm", 5));
    TEST_ASSERT_EQUAL(2, style_logic_named_ref(block, 3, 0, "big", 3));
    TEST_ASSERT_EQUAL(3, style_logic_named_ref(block, 3, 0, "night", 5));
    TEST_ASSERT_EQUAL(0, style_logic_named_ref(block, 3, 0, "nosuch", 6));
    // "big" was dropped at boot: unknown; the others keep their refs.
    TEST_ASSERT_EQUAL(1, style_logic_named_ref(block, 3, 0x2, "alarm", 5));
    TEST_ASSERT_EQUAL(0, style_logic_named_ref(block, 3, 0x2, "big", 3));
    TEST_ASSERT_EQUAL(3, style_logic_named_ref(block, 3, 0x2, "night", 5));
    TEST_ASSERT_EQUAL(3, style_logic_named_usable(3, 0));
    TEST_ASSERT_EQUAL(2, style_logic_named_usable(3, 0x2));
    TEST_ASSERT_EQUAL(0, style_logic_named_usable(3, 0x7));
    TEST_ASSERT_EQUAL(29, style_logic_named_usable(29, 0));
    TEST_ASSERT_EQUAL(28, style_logic_named_usable(29, 1u << 28));
}

// ---- an item's style list -----------------------------------------------------------

// The names of a "style" string, as ConfigManager and the script binding
// walk them, joined with '|'.
static void names_of(const char* text, char* out, size_t cap) {
    const char* cursor = text;
    const char* name = nullptr;
    size_t len = 0;
    size_t at = 0;
    out[0] = '\0';
    while (style_logic_list_next(&cursor, &name, &len)) {
        at += static_cast<size_t>(std::snprintf(out + at, cap - at, "%s%.*s", at ? "|" : "", static_cast<int>(len), name));
    }
}

void test_style_string_names() {
    char out[128];
    names_of("big night", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("big|night", out);
    names_of("alarm", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("alarm", out);
    // One or more spaces between names, and around them.
    names_of("  big   night  ", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("big|night", out);
    names_of("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
    names_of("   ", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
    // Only a space separates: a tab or a comma is part of a name (which is then no style name).
    names_of("a,b c\td", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("a,b|c\td", out);
    TEST_ASSERT_FALSE(style_logic_name_valid("a,b", 3));
    const char* cursor = nullptr;
    const char* name = nullptr;
    size_t len = 0;
    TEST_ASSERT_FALSE(style_logic_list_next(&cursor, &name, &len));
}

void test_list_add_order_and_duplicates() {
    style_list_t list = 0;
    TEST_ASSERT_EQUAL(0, style_logic_list_count(list));
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 5));
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 2));
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 9));
    TEST_ASSERT_EQUAL(3, style_logic_list_count(list));
    TEST_ASSERT_EQUAL(5, style_logic_list_ref(list, 0));
    TEST_ASSERT_EQUAL(2, style_logic_list_ref(list, 1));
    TEST_ASSERT_EQUAL(9, style_logic_list_ref(list, 2));
    TEST_ASSERT_EQUAL(0, style_logic_list_ref(list, 3));
    TEST_ASSERT_EQUAL(0, style_logic_list_ref(list, 4));   // past the list
    TEST_ASSERT_EQUAL_HEX32(0x00090205, list);

    // A name given twice counts once, at its last place.
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 5));
    TEST_ASSERT_EQUAL_HEX32(0x00050902, list);
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 5));   // already last: nothing changes
    TEST_ASSERT_EQUAL_HEX32(0x00050902, list);

    // Four names; a fifth is refused and nothing changes.
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 29));
    TEST_ASSERT_EQUAL_HEX32(0x1D050902, list);
    TEST_ASSERT_FALSE(style_logic_list_add(&list, 7));
    TEST_ASSERT_EQUAL_HEX32(0x1D050902, list);
    // One the list has still moves to the end when the list is full.
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 2));
    TEST_ASSERT_EQUAL_HEX32(0x021D0509, list);
    // No style (ref 0) is not a name.
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 0));
    TEST_ASSERT_EQUAL_HEX32(0x021D0509, list);
}

void test_list_remove() {
    style_list_t list = 0x04030201;
    style_logic_list_remove(&list, 2);
    TEST_ASSERT_EQUAL_HEX32(0x00040301, list);
    style_logic_list_remove(&list, 9);   // not in the list: nothing
    TEST_ASSERT_EQUAL_HEX32(0x00040301, list);
    style_logic_list_remove(&list, 0);
    TEST_ASSERT_EQUAL_HEX32(0x00040301, list);
    style_logic_list_remove(&list, 1);
    style_logic_list_remove(&list, 4);
    TEST_ASSERT_EQUAL_HEX32(0x00000003, list);
    style_logic_list_remove(&list, 3);
    TEST_ASSERT_EQUAL_HEX32(0, list);
}

// ---- the lookup -----------------------------------------------------------------------

void test_lookup_item_styles_then_kind_then_built_in() {
    style_props_t d[STYLE_EL_COUNT];
    style_logic_defaults(d);
    const style_props_t* kind = &d[STYLE_EL_DISPLAY_VALUE];   // system 14, white
    style_props_t r;

    // No style: the kind's (here the built-in) values.
    style_logic_lookup(kind, nullptr, 0, &r);
    TEST_ASSERT_EQUAL_MEMORY(kind, &r, sizeof(r));

    // "big alarm": the size of one, the colour of the other, the face of the kind.
    const style_props_t big = size(48);
    const style_props_t alarm = colour(0xFF0000);
    const style_props_t* layers[] = {&big, &alarm};
    style_logic_lookup(kind, layers, 2, &r);
    TEST_ASSERT_EQUAL(48, r.px);
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, r.color);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, r.face);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_FONT | STYLE_HAS_SIZE | STYLE_HAS_COLOR, r.has);
}

void test_lookup_the_last_name_wins_per_property() {
    style_props_t d[STYLE_EL_COUNT];
    style_logic_defaults(d);
    const style_props_t* kind = &d[STYLE_EL_KEY_LABEL];
    style_props_t a = colour(0x111111);
    a.has |= STYLE_HAS_SIZE | STYLE_HAS_LETTER_SPACE;
    a.px = 20;
    a.letter_space = 3;
    style_props_t b = colour(0x222222);
    b.has |= STYLE_HAS_SIZE;
    b.px = 40;
    const style_props_t c = colour(0x333333);
    style_props_t r;

    const style_props_t* abc[] = {&a, &b, &c};
    style_logic_lookup(kind, abc, 3, &r);
    TEST_ASSERT_EQUAL_HEX32(0x333333, r.color);   // all three set it: the last
    TEST_ASSERT_EQUAL(40, r.px);                  // a and b: b
    TEST_ASSERT_EQUAL(3, r.letter_space);         // only a

    const style_props_t* cba[] = {&c, &b, &a};
    style_logic_lookup(kind, cba, 3, &r);
    TEST_ASSERT_EQUAL_HEX32(0x111111, r.color);
    TEST_ASSERT_EQUAL(20, r.px);

    // Four styles, and one of them without a block for this part.
    const style_props_t d4 = face(2);
    const style_props_t* four[] = {&a, nullptr, &b, &d4};
    style_logic_lookup(kind, four, 4, &r);
    TEST_ASSERT_EQUAL_HEX32(0x222222, r.color);
    TEST_ASSERT_EQUAL(40, r.px);
    TEST_ASSERT_EQUAL(2, r.face);
}

void test_lookup_face_and_size_separately() {
    // "big semibold": the size of `big` and the face of `semibold`.
    style_props_t d[STYLE_EL_COUNT];
    style_logic_defaults(d);
    const style_props_t big = size(48);
    const style_props_t semibold = face(FONT_FACE_SYSTEM_BOLD);
    const style_props_t* layers[] = {&big, &semibold};
    style_props_t r;
    style_logic_lookup(&d[STYLE_EL_DISPLAY_VALUE], layers, 2, &r);
    TEST_ASSERT_EQUAL(48, r.px);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, r.face);
    // In the other order the same: they set different properties.
    const style_props_t* reversed[] = {&semibold, &big};
    style_logic_lookup(&d[STYLE_EL_DISPLAY_VALUE], reversed, 2, &r);
    TEST_ASSERT_EQUAL(48, r.px);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, r.face);
}

void test_lookup_per_part() {
    // A style {"color": red, "title": {"color": pink}} on a display item:
    // the value takes the main block, the caption the title block, the unit
    // nothing (the style has no "unit").
    style_props_t d[STYLE_EL_COUNT];
    style_logic_defaults(d);
    const style_props_t main = colour(0xFF0000);
    const style_props_t title = colour(0xFF8080);
    style_props_t r;

    const style_props_t* value[] = {&main};
    style_logic_lookup(&d[STYLE_EL_DISPLAY_VALUE], value, 1, &r);
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, r.color);
    const style_props_t* caption[] = {&title};
    style_logic_lookup(&d[STYLE_EL_DISPLAY_CAPTION], caption, 1, &r);
    TEST_ASSERT_EQUAL_HEX32(0xFF8080, r.color);
    TEST_ASSERT_EQUAL(14, r.px);
    const style_props_t* unit[] = {nullptr};
    style_logic_lookup(&d[STYLE_EL_DISPLAY_UNIT], unit, 1, &r);
    TEST_ASSERT_EQUAL_HEX32(0x999999, r.color);   // a main colour does not colour the unit
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR, r.has);

    // A style without "title" leaves the title as the kind has it.
    style_logic_lookup(&d[STYLE_EL_DISPLAY_CAPTION], unit, 1, &r);
    TEST_ASSERT_EQUAL_MEMORY(&d[STYLE_EL_DISPLAY_CAPTION], &r, sizeof(r));
}

void test_layers_set() {
    const style_props_t c = colour(0x123456);
    const style_props_t s = size(20);
    const style_props_t* layers[] = {&c, nullptr, &s};
    TEST_ASSERT_TRUE(style_logic_layers_set(layers, 3, STYLE_HAS_SIZE));
    TEST_ASSERT_TRUE(style_logic_layers_set(layers, 3, STYLE_HAS_COLOR));
    TEST_ASSERT_FALSE(style_logic_layers_set(layers, 3, STYLE_HAS_FONT));
    TEST_ASSERT_FALSE(style_logic_layers_set(layers, 2, STYLE_HAS_SIZE));
    TEST_ASSERT_FALSE(style_logic_layers_set(nullptr, 0, STYLE_HAS_SIZE));
}

void test_unit_follows_the_value() {
    style_props_t d[STYLE_EL_COUNT];
    style_logic_defaults(d);
    style_props_t value;
    style_props_t unit;
    int f = -9;
    int px = -9;

    // No styles: the value's built-in face and size, the unit's own colour.
    style_logic_lookup(&d[STYLE_EL_DISPLAY_VALUE], nullptr, 0, &value);
    style_logic_lookup(&d[STYLE_EL_DISPLAY_UNIT], nullptr, 0, &unit);
    style_logic_follow_font(&unit, value.face, value.px, &f, &px);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, f);
    TEST_ASSERT_EQUAL(14, px);
    TEST_ASSERT_EQUAL_HEX32(0x999999, unit.color);

    // The value's size from a style of the item: the unit follows.
    const style_props_t big = font(2, 48);
    const style_props_t* main[] = {&big};
    style_logic_lookup(&d[STYLE_EL_DISPLAY_VALUE], main, 1, &value);
    style_logic_follow_font(&unit, value.face, value.px, &f, &px);
    TEST_ASSERT_EQUAL(2, f);
    TEST_ASSERT_EQUAL(48, px);

    // A "unit" block of the item's styles that sets the size only: its
    // size, the value's face. Its colour too.
    style_props_t small = size(10);
    small.has |= STYLE_HAS_COLOR;
    small.color = 0x445566;
    const style_props_t* units[] = {&small};
    style_logic_lookup(&d[STYLE_EL_DISPLAY_UNIT], units, 1, &unit);
    style_logic_follow_font(&unit, value.face, value.px, &f, &px);
    TEST_ASSERT_EQUAL(2, f);
    TEST_ASSERT_EQUAL(10, px);
    TEST_ASSERT_EQUAL_HEX32(0x445566, unit.color);

    // "displayitem.unit" sets a face; a style of the item sets the size.
    style_props_t kindUnit = d[STYLE_EL_DISPLAY_UNIT];
    kindUnit.has |= STYLE_HAS_FONT;
    kindUnit.face = FONT_FACE_SYSTEM_BOLD;
    style_logic_lookup(&kindUnit, units, 1, &unit);
    style_logic_follow_font(&unit, value.face, value.px, &f, &px);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, f);
    TEST_ASSERT_EQUAL(10, px);
    // Without the item's unit block: the kind's face, the value's size.
    style_logic_lookup(&kindUnit, nullptr, 0, &unit);
    style_logic_follow_font(&unit, value.face, value.px, &f, &px);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, f);
    TEST_ASSERT_EQUAL(48, px);

    // The last of the item's styles whose unit block sets it.
    const style_props_t first = size(12);
    const style_props_t second = size(16);
    const style_props_t* two[] = {&first, &second};
    style_logic_lookup(&d[STYLE_EL_DISPLAY_UNIT], two, 2, &unit);
    style_logic_follow_font(&unit, value.face, value.px, &f, &px);
    TEST_ASSERT_EQUAL(16, px);
}

void test_selected_row_follows_the_rows() {
    style_props_t d[STYLE_EL_COUNT];
    style_logic_defaults(d);
    style_props_t rows;
    style_props_t selected;
    int f = -9;
    int px = -9;

    // A main block recolours and resizes the rows; the selected row keeps
    // its black on gold and takes the rows' font.
    style_props_t main = font(2, 24);
    main.has |= STYLE_HAS_COLOR;
    main.color = 0x00FF00;
    const style_props_t* mains[] = {&main};
    style_logic_lookup(&d[STYLE_EL_LIST], mains, 1, &rows);
    style_logic_lookup(&d[STYLE_EL_LIST_SELECTED], nullptr, 0, &selected);
    style_logic_follow_font(&selected, rows.face, rows.px, &f, &px);
    TEST_ASSERT_EQUAL(2, f);
    TEST_ASSERT_EQUAL(24, px);
    TEST_ASSERT_EQUAL_HEX32(0x000000, selected.color);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFD700, selected.bg);

    // A "selected" block that sets a size: that size, the rows' face.
    style_props_t pick = size(40);
    pick.has |= STYLE_HAS_BG;
    pick.bg = 0xFF00C853;
    const style_props_t* picks[] = {&pick};
    style_logic_lookup(&d[STYLE_EL_LIST_SELECTED], picks, 1, &selected);
    style_logic_follow_font(&selected, rows.face, rows.px, &f, &px);
    TEST_ASSERT_EQUAL(2, f);
    TEST_ASSERT_EQUAL(40, px);
    TEST_ASSERT_EQUAL_HEX32(0xFF00C853, selected.bg);
    TEST_ASSERT_EQUAL_HEX32(0x000000, selected.color);
}

// ---- fonts ------------------------------------------------------------------------------

void test_shared_font_both_one_none() {
    style_props_t d[STYLE_EL_COUNT];
    style_logic_defaults(d);
    const style_props_t* kind = &d[STYLE_EL_KEY_LABEL];   // system 32: holds a font
    int f = -9;
    int px = -9;

    // No style: the kind's font.
    TEST_ASSERT_TRUE(style_logic_shared_font(kind, nullptr, 0, &f, &px));
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, f);
    TEST_ASSERT_EQUAL(32, px);

    // A style with both holds its instance: no local font needed.
    const style_props_t both = font(2, 48);
    const style_props_t* one[] = {&both};
    style_props_t r;
    style_logic_lookup(kind, one, 1, &r);
    TEST_ASSERT_TRUE(style_logic_shared_font(kind, one, 1, &f, &px));
    TEST_ASSERT_EQUAL(r.face, f);
    TEST_ASSERT_EQUAL(r.px, px);

    // A style with one of the two holds none: the shared font is still the
    // kind's, which is not the lookup's: the component sets a local font.
    const style_props_t onlySize = size(48);
    const style_props_t* sized[] = {&onlySize};
    style_logic_lookup(kind, sized, 1, &r);
    TEST_ASSERT_TRUE(style_logic_shared_font(kind, sized, 1, &f, &px));
    TEST_ASSERT_EQUAL(32, px);
    TEST_ASSERT_EQUAL(48, r.px);
    const style_props_t onlyFace = face(2);
    const style_props_t* faced[] = {&onlyFace};
    style_logic_lookup(kind, faced, 1, &r);
    TEST_ASSERT_TRUE(style_logic_shared_font(kind, faced, 1, &f, &px));
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, f);
    TEST_ASSERT_EQUAL(2, r.face);

    // A style with neither: the kind's font is the lookup's.
    const style_props_t red = colour(0xFF0000);
    const style_props_t* coloured[] = {&red};
    style_logic_lookup(kind, coloured, 1, &r);
    TEST_ASSERT_TRUE(style_logic_shared_font(kind, coloured, 1, &f, &px));
    TEST_ASSERT_EQUAL(r.face, f);
    TEST_ASSERT_EQUAL(r.px, px);

    // Both, then a later style that changes the size: the shared font (the
    // style added last that holds one) is no longer the lookup's.
    const style_props_t* mixed[] = {&both, &onlySize, &red};
    const style_props_t other = size(20);
    const style_props_t* overridden[] = {&both, &other};
    style_logic_lookup(kind, overridden, 2, &r);
    TEST_ASSERT_TRUE(style_logic_shared_font(kind, overridden, 2, &f, &px));
    TEST_ASSERT_EQUAL(48, px);
    TEST_ASSERT_EQUAL(20, r.px);
    // Two styles with both: the last.
    const style_props_t both2 = font(FONT_FACE_SYSTEM_BOLD, 20);
    const style_props_t* two[] = {&both, &both2, nullptr};
    TEST_ASSERT_TRUE(style_logic_shared_font(kind, two, 3, &f, &px));
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, f);
    TEST_ASSERT_EQUAL(20, px);
    (void)mixed;

    // A part whose kind holds no font (the unit, the selected row): none,
    // unless a style of the item holds one.
    TEST_ASSERT_FALSE(style_logic_shared_font(&d[STYLE_EL_DISPLAY_UNIT], nullptr, 0, &f, &px));
    TEST_ASSERT_FALSE(style_logic_shared_font(&d[STYLE_EL_LIST_SELECTED], sized, 1, &f, &px));
    TEST_ASSERT_TRUE(style_logic_shared_font(&d[STYLE_EL_LIST_SELECTED], one, 1, &f, &px));
    TEST_ASSERT_EQUAL(2, f);
    TEST_ASSERT_EQUAL(48, px);
}

void test_what_goes_into_lvgl() {
    // A block that only sets a size (or a face, or a line-height) holds
    // nothing for LVGL: its style is not added to an object.
    const style_props_t onlySize = size(48);
    const style_props_t onlyFace = face(2);
    const style_props_t nothing = none();
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_lvgl_has(&onlySize, STYLE_PART_MAIN));
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_lvgl_has(&onlyFace, STYLE_PART_TITLE));
    style_props_t lh = none();
    lh.has = STYLE_HAS_LINE_HEIGHT;
    lh.line_height = 150;
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_lvgl_has(&lh, STYLE_PART_MAIN));
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_lvgl_has(&nothing, STYLE_PART_MAIN));

    // Both: one font instance.
    const style_props_t both = font(2, 48);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_FONT | STYLE_HAS_SIZE, style_logic_lvgl_has(&both, STYLE_PART_MAIN));

    style_props_t all = font(2, 48);
    all.has |= STYLE_HAS_COLOR | STYLE_HAS_OPA | STYLE_HAS_ALIGN | STYLE_HAS_LETTER_SPACE | STYLE_HAS_LINE_HEIGHT |
               STYLE_HAS_DECOR | STYLE_HAS_BG;
    const uint16_t text = STYLE_HAS_FONT | STYLE_HAS_SIZE | STYLE_HAS_COLOR | STYLE_HAS_OPA | STYLE_HAS_LETTER_SPACE |
                          STYLE_HAS_DECOR;
    // Main and title: with the alignment; no background (a main block's
    // background is the list's, set by the list itself).
    TEST_ASSERT_EQUAL_HEX16(text | STYLE_HAS_ALIGN, style_logic_lvgl_has(&all, STYLE_PART_MAIN));
    TEST_ASSERT_EQUAL_HEX16(text | STYLE_HAS_ALIGN, style_logic_lvgl_has(&all, STYLE_PART_TITLE));
    // Unit: text-align is ignored.
    TEST_ASSERT_EQUAL_HEX16(text, style_logic_lvgl_has(&all, STYLE_PART_UNIT));
    // Selected: text-align is ignored; the background of the row.
    TEST_ASSERT_EQUAL_HEX16(text | STYLE_HAS_BG, style_logic_lvgl_has(&all, STYLE_PART_SELECTED));

    // A main block with only a background: nothing to add.
    style_props_t bg = none();
    bg.has = STYLE_HAS_BG;
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_lvgl_has(&bg, STYLE_PART_MAIN));
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_BG, style_logic_lvgl_has(&bg, STYLE_PART_SELECTED));
}

void test_what_a_span_label_takes() {
    // Font and colour are local on a span label: a style that only sets
    // those is not added to it.
    style_props_t p = font(2, 48);
    p.has |= STYLE_HAS_COLOR;
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_span_has(&p, STYLE_PART_MAIN));
    p.has |= STYLE_HAS_OPA;
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_OPA, style_logic_span_has(&p, STYLE_PART_MAIN));
    p.has = STYLE_HAS_LETTER_SPACE | STYLE_HAS_DECOR | STYLE_HAS_ALIGN | STYLE_HAS_BG | STYLE_HAS_LINE_HEIGHT;
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_LETTER_SPACE | STYLE_HAS_DECOR | STYLE_HAS_ALIGN,
                            style_logic_span_has(&p, STYLE_PART_TITLE));
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_LETTER_SPACE | STYLE_HAS_DECOR, style_logic_span_has(&p, STYLE_PART_UNIT));
    p.has = STYLE_HAS_ALIGN;
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_span_has(&p, STYLE_PART_UNIT));
}

// ---- text-align, line-height, the icon ----------------------------------------------------

void test_text_align_of_a_part() {
    style_props_t d[STYLE_EL_COUNT];
    style_logic_defaults(d);
    style_props_t r;
    // No style sets one: none (a display item part is then as wide as its
    // text, a button label centred, a list left aligned).
    for (int el = 0; el < STYLE_EL_COUNT; el++) {
        TEST_ASSERT_EQUAL(0, style_logic_text_align(&d[el]));
    }
    // The last of the item's styles that sets it, else the kind's.
    style_props_t centre = none();
    set_s(centre, "text-align", "center");
    style_props_t right = none();
    set_s(right, "text-align", "right");
    const style_props_t red = colour(0xFF0000);
    const style_props_t* layers[] = {&centre, &right, &red};
    style_logic_lookup(&d[STYLE_EL_DISPLAY_VALUE], layers, 3, &r);
    TEST_ASSERT_EQUAL(STYLE_ALIGN_RIGHT, style_logic_text_align(&r));
    style_logic_lookup(&d[STYLE_EL_DISPLAY_VALUE], layers, 1, &r);
    TEST_ASSERT_EQUAL(STYLE_ALIGN_CENTER, style_logic_text_align(&r));
    style_props_t kind = d[STYLE_EL_DISPLAY_VALUE];
    set_s(kind, "text-align", "left");
    const style_props_t* coloured[] = {&red};
    style_logic_lookup(&kind, coloured, 1, &r);
    TEST_ASSERT_EQUAL(STYLE_ALIGN_LEFT, style_logic_text_align(&r));
}

void test_line_space() {
    // 1.25: a quarter of the line, exactly what a list always had.
    for (int h = 0; h <= 200; h++) {
        TEST_ASSERT_EQUAL(h / 4, style_logic_line_space(h, 125));
    }
    TEST_ASSERT_EQUAL(0, style_logic_line_space(39, 100));
    TEST_ASSERT_EQUAL(19, style_logic_line_space(39, 150));    // 19.5
    TEST_ASSERT_EQUAL(78, style_logic_line_space(39, 300));
    TEST_ASSERT_EQUAL(-7, style_logic_line_space(39, 80));     // -7.8: lines closer together
    TEST_ASSERT_EQUAL(8, style_logic_line_space(32, 125));
}

void test_icon_color() {
    uint32_t rgb = 0x123456;
    // No "icon" block anywhere: the caller uses the main text colour.
    TEST_ASSERT_FALSE(style_logic_icon_color(0, nullptr, 0, &rgb));
    TEST_ASSERT_EQUAL_HEX32(0x123456, rgb);
    // The kind's.
    TEST_ASSERT_TRUE(style_logic_icon_color(STYLE_ICON_SET | 0x00FF00, nullptr, 0, &rgb));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, rgb);
    // The last of the item's styles that has one, over the kind's.
    const uint32_t layers[] = {STYLE_ICON_SET | 0xFF0000, 0, STYLE_ICON_SET | 0x0000FF, 0};
    TEST_ASSERT_TRUE(style_logic_icon_color(STYLE_ICON_SET | 0x00FF00, layers, 4, &rgb));
    TEST_ASSERT_EQUAL_HEX32(0x0000FF, rgb);
    TEST_ASSERT_TRUE(style_logic_icon_color(0, layers, 1, &rgb));
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, rgb);
    // Styles without an "icon" block: the kind's stays.
    const uint32_t no_icon[] = {0, 0};
    TEST_ASSERT_TRUE(style_logic_icon_color(STYLE_ICON_SET | 0x00FF00, no_icon, 2, &rgb));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, rgb);
    // Black is a colour.
    const uint32_t black[] = {STYLE_ICON_SET};
    TEST_ASSERT_TRUE(style_logic_icon_color(STYLE_ICON_SET | 0x00FF00, black, 1, &rgb));
    TEST_ASSERT_EQUAL_HEX32(0x000000, rgb);
}

// ---- fb.style.set ----------------------------------------------------------------------------

void test_split_a_dotted_property() {
    style_part_t part = STYLE_PART_UNIT;
    TEST_ASSERT_EQUAL_STRING("color", style_logic_split_prop("color", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_MAIN, part);
    TEST_ASSERT_EQUAL_STRING("color", style_logic_split_prop("title.color", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_TITLE, part);
    TEST_ASSERT_EQUAL_STRING("letter-spacing", style_logic_split_prop("unit.letter-spacing", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_UNIT, part);
    TEST_ASSERT_EQUAL_STRING("background-color", style_logic_split_prop("selected.background-color", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_SELECTED, part);
    TEST_ASSERT_EQUAL_STRING("color", style_logic_split_prop("icon.color", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_ICON, part);
    // A prefix that is no part: a main property of that whole name, which is then unknown.
    TEST_ASSERT_EQUAL_STRING("main.color", style_logic_split_prop("main.color", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_MAIN, part);
    style_props_t p = colour(0);
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_set_string(&p, "main.color", "#ffffff"));
    TEST_ASSERT_EQUAL_STRING("Title.color", style_logic_split_prop("Title.color", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_MAIN, part);
    TEST_ASSERT_EQUAL_STRING(".color", style_logic_split_prop(".color", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_MAIN, part);
    // Only one prefix: "title.unit.color" is the title's property "unit.color" (unknown).
    TEST_ASSERT_EQUAL_STRING("unit.color", style_logic_split_prop("title.unit.color", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_TITLE, part);
    TEST_ASSERT_EQUAL_STRING("", style_logic_split_prop("title.", &part));
    TEST_ASSERT_EQUAL(STYLE_PART_TITLE, part);
    TEST_ASSERT_NULL(style_logic_split_prop(nullptr, &part));
}

// fb.style.set: every property but the three that depend on the font, in a
// block that exists; one the block did not set is added.
void test_script_set_changes_and_adds() {
    style_props_t p = none();
    set_s(p, "color", "#ff0000");
    set_n(p, "letter-spacing", 2);
    set_n(p, "opacity", 100);
    set_s(p, "background-color", "#000000");
    set_n(p, "font-size", 20);
    set_n(p, "line-height", 150);
    const style_props_t before = p;

    // What the block sets: another value, no property more.
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_string(&p, "color", "#00ff00"));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, p.color);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_number(&p, "letter-spacing", -3));
    TEST_ASSERT_EQUAL(-3, p.letter_space);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_number(&p, "opacity", 50));
    TEST_ASSERT_EQUAL(128, p.opa);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_string(&p, "background-color", "#11223380"));
    TEST_ASSERT_EQUAL_HEX32(0x80112233, p.bg);
    TEST_ASSERT_EQUAL_HEX16(before.has, p.has);

    // What it does not set is added: the has bit says so.
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_string(&p, "text-align", "center"));
    TEST_ASSERT_EQUAL(STYLE_ALIGN_CENTER, p.align);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_string(&p, "text-decoration", "underline"));
    TEST_ASSERT_EQUAL(STYLE_DECOR_UNDERLINE, p.decor);
    TEST_ASSERT_EQUAL_HEX16(before.has | STYLE_HAS_ALIGN | STYLE_HAS_DECOR, p.has);

    // The font, the size and the line height are per font: fixed, set or not.
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_number(&p, "font-size", 30));
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_string(&p, "font-family", "mdi"));
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_number(&p, "line-height", 200));
    TEST_ASSERT_EQUAL(20, p.px);
    TEST_ASSERT_EQUAL(150, p.line_height);
    // Unknown, and values that are not valid: as in config.
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_set_string(&p, "text_color", "#ffffff"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_set_number(&p, "font-weight", 700));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_set_string(&p, "color", "green"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_set_number(&p, "color", 1));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_set_number(&p, "letter-spacing", 500));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_set_string(&p, "letter-spacing", "2"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_set_number(&p, "opacity", 101));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, p.color);
    TEST_ASSERT_EQUAL(-3, p.letter_space);
    TEST_ASSERT_EQUAL(128, p.opa);

    // A block that sets nothing takes every property but the fixed ones; a
    // bad value adds nothing.
    static const struct { const char* prop; const char* text; int32_t number; uint16_t bit; } add[] = {
        {"color", "#ffffff", 0, STYLE_HAS_COLOR},
        {"background-color", "#00000080", 0, STYLE_HAS_BG},
        {"text-align", "right", 0, STYLE_HAS_ALIGN},
        {"text-decoration", "line-through", 0, STYLE_HAS_DECOR},
        {"opacity", nullptr, 40, STYLE_HAS_OPA},
        {"letter-spacing", nullptr, 3, STYLE_HAS_LETTER_SPACE},
    };
    for (const auto& a : add) {
        style_props_t empty = none();
        TEST_ASSERT_EQUAL_MESSAGE(STYLE_PROP_BAD_VALUE,
                                  a.text ? style_logic_script_set_string(&empty, a.prop, "?")
                                         : style_logic_script_set_number(&empty, a.prop, STYLE_NUMBER_INVALID),
                                  a.prop);
        TEST_ASSERT_EQUAL_HEX16(0, empty.has);
        TEST_ASSERT_EQUAL_MESSAGE(STYLE_PROP_OK,
                                  a.text ? style_logic_script_set_string(&empty, a.prop, a.text)
                                         : style_logic_script_set_number(&empty, a.prop, a.number),
                                  a.prop);
        TEST_ASSERT_EQUAL_HEX16(a.bit, empty.has);
    }
    style_props_t empty = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_number(&empty, "font-size", 20));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_set_string(&empty, "", "#ffffff"));
    TEST_ASSERT_EQUAL_HEX16(0, empty.has);
}

// The order of the checks: the name, the fixed three, (the block: the
// caller's), the value. A number that is none (14.5 for font-size) must not
// hide that the property is fixed.
void test_script_set_order_of_checks() {
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_settable("colour"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_settable(""));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_settable(nullptr));
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_settable("font-family"));
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_settable("font-size"));
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_settable("line-height"));
    for (const char* prop : {"color", "background-color", "text-align", "text-decoration", "opacity", "letter-spacing"}) {
        TEST_ASSERT_EQUAL_MESSAGE(STYLE_PROP_OK, style_logic_script_settable(prop), prop);
    }

    style_props_t p = none();
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_number(&p, "font-size", STYLE_NUMBER_INVALID));
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_number(&p, "line-height", STYLE_NUMBER_INVALID));
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_number(&p, "font-family", STYLE_NUMBER_INVALID));
    TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_string(&p, "font-size", "big"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_set_number(&p, "font-weight", STYLE_NUMBER_INVALID));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_set_number(&p, "opacity", STYLE_NUMBER_INVALID));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_set_number(&p, "letter-spacing", STYLE_NUMBER_INVALID));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_set_number(&p, "color", STYLE_NUMBER_INVALID));
    TEST_ASSERT_EQUAL_HEX16(0, p.has);
}

// fb.style.set on "button" / "displayitem" / "list": the element as
// resolved (built in, then config); a property it does not hold is added.
void test_script_set_on_the_style_of_a_kind() {
    style_props_t el[STYLE_EL_COUNT];
    style_logic_resolve(nullptr, el);

    for (int i = 0; i < STYLE_EL_COUNT; i++) {
        style_props_t p = el[i];
        TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_string(&p, "color", "#123456"));
        TEST_ASSERT_EQUAL_HEX32(0x123456, p.color);
        TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_number(&p, "font-size", 20));
        TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_string(&p, "font-family", "mdi"));
        TEST_ASSERT_EQUAL(STYLE_PROP_FIXED, style_logic_script_set_number(&p, "line-height", 150));
        TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_set_string(&p, "colour", "#123456"));
        // what the built-in look does not hold is added
        TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_number(&p, "opacity", 50));
        TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_number(&p, "letter-spacing", 2));
        TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_string(&p, "text-align", "right"));
        TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_string(&p, "background-color", "#00ff0080"));
        TEST_ASSERT_EQUAL_HEX16(el[i].has | STYLE_HAS_OPA | STYLE_HAS_LETTER_SPACE | STYLE_HAS_ALIGN | STYLE_HAS_BG, p.has);
        TEST_ASSERT_EQUAL_HEX32(0x8000ff00, p.bg);
        // the fonts and the line height stay what they were
        TEST_ASSERT_EQUAL(el[i].face, p.face);
        TEST_ASSERT_EQUAL(el[i].px, p.px);
        TEST_ASSERT_EQUAL(el[i].line_height, p.line_height);
    }

    // a part a kind does not have: no element, so nothing to change
    TEST_ASSERT_EQUAL(-1, style_logic_element(STYLE_KIND_BUTTON, STYLE_PART_UNIT));
    TEST_ASSERT_EQUAL(-1, style_logic_element(STYLE_KIND_LIST, STYLE_PART_TITLE));
    TEST_ASSERT_EQUAL(-1, style_logic_element(STYLE_KIND_DISPLAYITEM, STYLE_PART_SELECTED));

    // the icon colour: set or not before
    uint32_t icon = 0;
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_icon_set_string(&icon, "color", "#ff0000"));
    TEST_ASSERT_EQUAL_HEX32(STYLE_ICON_SET | 0xff0000, icon);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_icon_set_string(&icon, "color", "#00ff00"));
    TEST_ASSERT_EQUAL_HEX32(STYLE_ICON_SET | 0x00ff00, icon);
}

// A block that got its first LVGL property from a script is added to the
// objects from then on; with the property gone again (restart) it is not.
void test_added_property_makes_a_block_attachable() {
    style_props_t p = none();
    set_n(p, "font-size", 48);
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_lvgl_has(&p, STYLE_PART_MAIN));
    const style_props_t config = p;
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_string(&p, "color", "#ff0000"));
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR, style_logic_lvgl_has(&p, STYLE_PART_MAIN));
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_number(&p, "opacity", 40));
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_OPA, style_logic_span_has(&p, STYLE_PART_MAIN));
    // a background in a main block is the list's to draw: nothing for LVGL
    style_props_t bg = config;
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_set_string(&bg, "background-color", "#000000"));
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_lvgl_has(&bg, STYLE_PART_MAIN));
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_BG, style_logic_lvgl_has(&bg, STYLE_PART_SELECTED));
    // the restore puts the config values back, has bits included
    p = config;
    TEST_ASSERT_EQUAL_HEX16(0, style_logic_lvgl_has(&p, STYLE_PART_MAIN));
}

// The selected row of a list: its own block, else the rows' for opacity,
// letter-spacing and text-decoration (LVGL takes them from the roller).
void test_selected_row_takes_text_from_the_rows() {
    style_props_t el[STYLE_EL_COUNT];
    style_logic_resolve(nullptr, el);
    style_props_t rows = el[STYLE_EL_LIST];
    style_props_t selected = el[STYLE_EL_LIST_SELECTED];
    style_props_t out;

    // nothing set: nothing taken over
    style_logic_selected_row(&selected, &rows, &out);
    TEST_ASSERT_EQUAL_HEX16(selected.has, out.has);

    set_n(rows, "opacity", 50);
    set_n(rows, "letter-spacing", 4);
    set_s(rows, "text-decoration", "underline");
    set_s(rows, "text-align", "right");
    set_s(rows, "background-color", "#112233");
    style_logic_selected_row(&selected, &rows, &out);
    TEST_ASSERT_EQUAL_HEX16(selected.has | STYLE_HAS_OPA | STYLE_HAS_LETTER_SPACE | STYLE_HAS_DECOR, out.has);
    TEST_ASSERT_EQUAL(128, out.opa);
    TEST_ASSERT_EQUAL(4, out.letter_space);
    TEST_ASSERT_EQUAL(STYLE_DECOR_UNDERLINE, out.decor);
    // never the colour or the background of the rows
    TEST_ASSERT_EQUAL_HEX32(0x000000, out.color);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFD700, out.bg);

    // what "selected" sets itself wins, property by property
    set_n(selected, "opacity", 100);
    set_s(selected, "text-decoration", "none");
    style_logic_selected_row(&selected, &rows, &out);
    TEST_ASSERT_EQUAL(255, out.opa);
    TEST_ASSERT_EQUAL(STYLE_DECOR_NONE, out.decor);
    TEST_ASSERT_EQUAL(4, out.letter_space);
}

// Which items draw again after fb.style.set: those of a kind whose style
// changed, and those that list a changed style.
void test_changed_styles_reach_their_users() {
    style_list_t list = 0;
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 3));
    TEST_ASSERT_TRUE(style_logic_list_add(&list, STYLE_NAMED_MAX));

    TEST_ASSERT_FALSE(style_logic_changed(0, STYLE_KIND_BUTTON, list));
    TEST_ASSERT_TRUE(style_logic_changed(1u << 2, STYLE_KIND_BUTTON, list));                   // ref 3
    TEST_ASSERT_TRUE(style_logic_changed(1u << (STYLE_NAMED_MAX - 1), STYLE_KIND_LIST, list)); // the last ref
    TEST_ASSERT_FALSE(style_logic_changed(1u << 3, STYLE_KIND_BUTTON, list));                  // ref 4: not listed
    TEST_ASSERT_FALSE(style_logic_changed(1u << 2, STYLE_KIND_BUTTON, 0));

    // a kind: every item of it, with or without styles; no other kind
    TEST_ASSERT_TRUE(style_logic_changed(STYLE_CHANGED_KIND(STYLE_KIND_BUTTON), STYLE_KIND_BUTTON, 0));
    TEST_ASSERT_TRUE(style_logic_changed(STYLE_CHANGED_KIND(STYLE_KIND_BUTTON), STYLE_KIND_BUTTON, list));
    TEST_ASSERT_FALSE(style_logic_changed(STYLE_CHANGED_KIND(STYLE_KIND_BUTTON), STYLE_KIND_DISPLAYITEM, list));
    TEST_ASSERT_FALSE(style_logic_changed(STYLE_CHANGED_KIND(STYLE_KIND_DISPLAYITEM), STYLE_KIND_LIST, 0));
    TEST_ASSERT_TRUE(style_logic_changed(STYLE_CHANGED_KIND(STYLE_KIND_LIST), STYLE_KIND_LIST, 0));

    // the bits of the kinds are not bits of the table
    for (int k = 0; k < STYLE_KIND_COUNT; k++) {
        TEST_ASSERT_TRUE(STYLE_CHANGED_KIND(k) > (1u << (STYLE_NAMED_MAX - 1)));
        TEST_ASSERT_NOT_EQUAL(0u, STYLE_CHANGED_KIND(k));
    }
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, STYLE_CHANGED_KIND(STYLE_KIND_COUNT - 1));
}

// addStyle then removeStyle, as a script switches a state on and off.
void test_list_add_then_remove_gives_the_list_back() {
    style_list_t configured = 0;
    TEST_ASSERT_TRUE(style_logic_list_add(&configured, 4));
    TEST_ASSERT_TRUE(style_logic_list_add(&configured, 1));
    style_list_t list = configured;
    TEST_ASSERT_TRUE(style_logic_list_add(&list, 7));
    TEST_ASSERT_EQUAL(3, style_logic_list_count(list));
    TEST_ASSERT_EQUAL(7, style_logic_list_ref(list, 2));
    style_logic_list_remove(&list, 7);
    TEST_ASSERT_EQUAL_HEX32(configured, list);
    style_logic_list_remove(&list, 7); // not there: nothing changes
    TEST_ASSERT_EQUAL_HEX32(configured, list);
    // a configured one can be taken away; the others move up
    style_logic_list_remove(&list, 4);
    TEST_ASSERT_EQUAL(1, style_logic_list_count(list));
    TEST_ASSERT_EQUAL(1, style_logic_list_ref(list, 0));
    // a full list: adding one it has moves it, adding another is refused
    style_list_t full = 0;
    for (uint8_t r = 1; r <= STYLE_ITEM_MAX; r++) TEST_ASSERT_TRUE(style_logic_list_add(&full, r));
    const style_list_t before = full;
    TEST_ASSERT_FALSE(style_logic_list_add(&full, 9));
    TEST_ASSERT_EQUAL_HEX32(before, full);
    TEST_ASSERT_TRUE(style_logic_list_add(&full, 1));
    TEST_ASSERT_EQUAL(1, style_logic_list_ref(full, STYLE_ITEM_MAX - 1));
    TEST_ASSERT_EQUAL(2, style_logic_list_ref(full, 0));
}

void test_script_set_icon_color() {
    uint32_t icon = STYLE_ICON_SET | 0xFF0000;
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_icon_set_string(&icon, "color", "#0000ff"));
    TEST_ASSERT_EQUAL_HEX32(STYLE_ICON_SET | 0x0000FF, icon);
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_icon_set_string(&icon, "color", "blue"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, style_logic_script_icon_set_string(&icon, "opacity", "1"));
    TEST_ASSERT_EQUAL_HEX32(STYLE_ICON_SET | 0x0000FF, icon);
    // A style without an "icon" colour gets one; a bad value leaves it without.
    uint32_t unset = 0;
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, style_logic_script_icon_set_string(&unset, "color", "0000ff"));
    TEST_ASSERT_EQUAL_HEX32(0, unset);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, style_logic_script_icon_set_string(&unset, "color", "#0000ff"));
    TEST_ASSERT_EQUAL_HEX32(STYLE_ICON_SET | 0x0000FF, unset);
}

// ---- keys the config no longer has -----------------------------------------------------------

static int removed(config_scope_t scope, const char* key, const char** hint = nullptr) {
    return style_logic_removed_key(scope, key, std::strlen(key), hint);
}

void test_removed_keys() {
    // The old block and the vendor's list names, at the top only.
    const char* hint = nullptr;
    TEST_ASSERT_TRUE(removed(CONFIG_SCOPE_TOP, "style", &hint) >= 0);
    TEST_ASSERT_NOT_NULL(std::strstr(hint, "\"fonts\""));
    TEST_ASSERT_NOT_NULL(std::strstr(hint, "\"styles\""));
    TEST_ASSERT_TRUE(removed(CONFIG_SCOPE_TOP, "mqttbuttons", &hint) >= 0);
    TEST_ASSERT_NOT_NULL(std::strstr(hint, "\"buttons\""));
    TEST_ASSERT_TRUE(removed(CONFIG_SCOPE_TOP, "mqttdisplays", &hint) >= 0);
    TEST_ASSERT_NOT_NULL(std::strstr(hint, "\"displayitems\""));
    TEST_ASSERT_TRUE(removed(CONFIG_SCOPE_TOP, "mqttbrokers", &hint) >= 0);
    TEST_ASSERT_NOT_NULL(std::strstr(hint, "\"brokers\""));

    // An item's "style" is its style list: not removed.
    TEST_ASSERT_EQUAL(-1, removed(CONFIG_SCOPE_BUTTON, "style"));
    TEST_ASSERT_EQUAL(-1, removed(CONFIG_SCOPE_DISPLAYITEM, "style"));
    TEST_ASSERT_EQUAL(-1, removed(CONFIG_SCOPE_LIST, "style"));

    // The font fields: "font" and "fontsize" on every item, the others on their kind.
    for (const config_scope_t scope : {CONFIG_SCOPE_BUTTON, CONFIG_SCOPE_DISPLAYITEM, CONFIG_SCOPE_LIST}) {
        TEST_ASSERT_TRUE(removed(scope, "font", &hint) >= 0);
        TEST_ASSERT_NOT_NULL(std::strstr(hint, "font-family"));
        TEST_ASSERT_TRUE(removed(scope, "fontsize", &hint) >= 0);
        TEST_ASSERT_NOT_NULL(std::strstr(hint, "font-size"));
    }
    const char* const button[] = {"topfont", "topfontsize", "topstyle", "topLabel", "leds", "buttonuid", "id"};
    for (const char* key : button) {
        TEST_ASSERT_TRUE_MESSAGE(removed(CONFIG_SCOPE_BUTTON, key) >= 0, key);
        TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_DISPLAYITEM, key), key);
        TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_LIST, key), key);
        TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_TOP, key), key);
    }
    const char* const item[] = {"labelfont", "labelfontsize", "labelstyle", "iconcolor"};
    for (const char* key : item) {
        TEST_ASSERT_TRUE_MESSAGE(removed(CONFIG_SCOPE_DISPLAYITEM, key) >= 0, key);
        TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_BUTTON, key), key);
        TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_LIST, key), key);
    }
    const char* const list[] = {"color", "focuscolor", "focustextcolor", "focusstyle"};
    for (const char* key : list) {
        TEST_ASSERT_TRUE_MESSAGE(removed(CONFIG_SCOPE_LIST, key) >= 0, key);
        TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_BUTTON, key), key);
        TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_DISPLAYITEM, key), key);
    }
    TEST_ASSERT_TRUE(removed(CONFIG_SCOPE_DISPLAYITEM, "iconcolor", &hint) >= 0);
    TEST_ASSERT_NOT_NULL(std::strstr(hint, "\"icon\""));
    TEST_ASSERT_TRUE(removed(CONFIG_SCOPE_BUTTON, "topLabel", &hint) >= 0);
    TEST_ASSERT_NOT_NULL(std::strstr(hint, "\"toplabel\""));
    TEST_ASSERT_TRUE(removed(CONFIG_SCOPE_BROKER, "autoDiscovery", &hint) >= 0);
    // The key was an alias of "hassdiscovery": a config with "autoDiscovery":
    // false has discovery ON now, and the warning has to say so.
    TEST_ASSERT_EQUAL_STRING("use \"hassdiscovery\"; Home Assistant discovery is on unless \"hassdiscovery\" is false", hint);
    TEST_ASSERT_NOT_NULL(std::strstr(hint, "\"hassdiscovery\""));
    TEST_ASSERT_TRUE(removed(CONFIG_SCOPE_BUTTON, "leds", &hint) >= 0);
    TEST_ASSERT_EQUAL_STRING("put the topics in \"topics\"; the topics under \"leds\" are no longer subscribed", hint);

    // What the config has: not reported.
    const char* const kept_button[] = {"name", "position", "page", "label", "toplabel", "ledcolorfront",
                                       "ledcolorwall", "topics", "icon", "iconsize", "style"};
    for (const char* key : kept_button) TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_BUTTON, key), key);
    const char* const kept_item[] = {"name", "displayitemid", "x", "y", "width", "page", "boxtype", "align", "label",
                                     "unit", "round", "topics", "icon", "iconsize", "style", "id", "color"};
    for (const char* key : kept_item) TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_DISPLAYITEM, key), key);
    const char* const kept_list[] = {"name", "page", "x", "y", "width", "rows", "items", "selected", "style"};
    for (const char* key : kept_list) TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_LIST, key), key);
    const char* const kept_top[] = {"fonts", "styles", "buttons", "displayitems", "brokers", "lists", "images",
                                    "clocks", "core", "icons", "info", "sensors", "font", "fontsize"};
    for (const char* key : kept_top) TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_TOP, key), key);
    const char* const kept_broker[] = {"brokerid", "url", "port", "wsport", "username", "password", "hassdiscovery"};
    for (const char* key : kept_broker) TEST_ASSERT_EQUAL_MESSAGE(-1, removed(CONFIG_SCOPE_BROKER, key), key);

    // Exact names, exact length.
    TEST_ASSERT_EQUAL(-1, removed(CONFIG_SCOPE_BUTTON, "Font"));
    TEST_ASSERT_EQUAL(-1, removed(CONFIG_SCOPE_BUTTON, "fontsizes"));
    TEST_ASSERT_TRUE(style_logic_removed_key(CONFIG_SCOPE_BUTTON, "fontsize", 4, nullptr) >= 0);   // "font"
    TEST_ASSERT_EQUAL(-1, style_logic_removed_key(CONFIG_SCOPE_BUTTON, "fontsize", 5, nullptr));   // "fonts"
    TEST_ASSERT_EQUAL(-1, style_logic_removed_key(CONFIG_SCOPE_BUTTON, nullptr, 0, nullptr));
    TEST_ASSERT_EQUAL(-1, style_logic_removed_key(CONFIG_SCOPE_COUNT, "font", 4, nullptr));
}

void test_removed_keys_are_numbered_once_per_name() {
    // One number per key name, the same in every scope, so that a key is
    // reported once however often and wherever the file has it.
    const int font = removed(CONFIG_SCOPE_BUTTON, "font");
    TEST_ASSERT_EQUAL(font, removed(CONFIG_SCOPE_DISPLAYITEM, "font"));
    TEST_ASSERT_EQUAL(font, removed(CONFIG_SCOPE_LIST, "font"));
    const int fontsize = removed(CONFIG_SCOPE_LIST, "fontsize");
    TEST_ASSERT_EQUAL(fontsize, removed(CONFIG_SCOPE_BUTTON, "fontsize"));
    TEST_ASSERT_NOT_EQUAL(font, fontsize);

    const struct {
        config_scope_t scope;
        const char* key;
    } all[] = {
        {CONFIG_SCOPE_TOP, "style"}, {CONFIG_SCOPE_TOP, "mqttbuttons"}, {CONFIG_SCOPE_TOP, "mqttdisplays"},
        {CONFIG_SCOPE_TOP, "mqttbrokers"}, {CONFIG_SCOPE_BUTTON, "font"}, {CONFIG_SCOPE_BUTTON, "fontsize"},
        {CONFIG_SCOPE_BUTTON, "topfont"}, {CONFIG_SCOPE_BUTTON, "topfontsize"}, {CONFIG_SCOPE_BUTTON, "topstyle"},
        {CONFIG_SCOPE_BUTTON, "topLabel"}, {CONFIG_SCOPE_BUTTON, "leds"}, {CONFIG_SCOPE_BUTTON, "buttonuid"},
        {CONFIG_SCOPE_BUTTON, "id"}, {CONFIG_SCOPE_DISPLAYITEM, "labelfont"},
        {CONFIG_SCOPE_DISPLAYITEM, "labelfontsize"}, {CONFIG_SCOPE_DISPLAYITEM, "labelstyle"},
        {CONFIG_SCOPE_DISPLAYITEM, "iconcolor"}, {CONFIG_SCOPE_LIST, "color"}, {CONFIG_SCOPE_LIST, "focuscolor"},
        {CONFIG_SCOPE_LIST, "focustextcolor"}, {CONFIG_SCOPE_LIST, "focusstyle"},
        {CONFIG_SCOPE_BROKER, "autoDiscovery"},
    };
    uint32_t seen = 0;
    for (const auto& entry : all) {
        const char* hint = nullptr;
        const int index = removed(entry.scope, entry.key, &hint);
        TEST_ASSERT_TRUE_MESSAGE(index >= 0 && index < CONFIG_REMOVED_KEYS, entry.key);
        TEST_ASSERT_TRUE_MESSAGE(hint != nullptr && hint[0] != '\0', entry.key);   // every one names its replacement
        TEST_ASSERT_EQUAL_MESSAGE(0, seen & (1u << index), entry.key);             // a number of its own
        seen |= 1u << index;
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_kinds_parts_and_elements);
    RUN_TEST(test_resolve_without_styles_is_the_built_in_look);
    RUN_TEST(test_resolve_a_reserved_style_changes_its_part_only);
    RUN_TEST(test_resolve_displayitem_main_does_not_reach_unit_or_caption);
    RUN_TEST(test_resolve_list_main_does_not_reach_the_selected_row);
    RUN_TEST(test_item_px_steps_of_the_markup);
    RUN_TEST(test_color);
    RUN_TEST(test_parse_color_and_background);
    RUN_TEST(test_background_color);
    RUN_TEST(test_font_family);
    RUN_TEST(test_font_size);
    RUN_TEST(test_text_align);
    RUN_TEST(test_letter_spacing);
    RUN_TEST(test_text_decoration);
    RUN_TEST(test_opacity_in_hundredths);
    RUN_TEST(test_line_height_in_hundredths);
    RUN_TEST(test_prop_scale);
    RUN_TEST(test_unknown_properties);
    RUN_TEST(test_icon_block);
    RUN_TEST(test_names);
    RUN_TEST(test_the_limit_of_32_styles);
    RUN_TEST(test_names_block);
    RUN_TEST(test_dropped_styles_are_unknown);
    RUN_TEST(test_style_string_names);
    RUN_TEST(test_list_add_order_and_duplicates);
    RUN_TEST(test_list_remove);
    RUN_TEST(test_lookup_item_styles_then_kind_then_built_in);
    RUN_TEST(test_lookup_the_last_name_wins_per_property);
    RUN_TEST(test_lookup_face_and_size_separately);
    RUN_TEST(test_lookup_per_part);
    RUN_TEST(test_layers_set);
    RUN_TEST(test_unit_follows_the_value);
    RUN_TEST(test_selected_row_follows_the_rows);
    RUN_TEST(test_shared_font_both_one_none);
    RUN_TEST(test_what_goes_into_lvgl);
    RUN_TEST(test_what_a_span_label_takes);
    RUN_TEST(test_text_align_of_a_part);
    RUN_TEST(test_line_space);
    RUN_TEST(test_icon_color);
    RUN_TEST(test_split_a_dotted_property);
    RUN_TEST(test_script_set_changes_and_adds);
    RUN_TEST(test_script_set_order_of_checks);
    RUN_TEST(test_added_property_makes_a_block_attachable);
    RUN_TEST(test_selected_row_takes_text_from_the_rows);
    RUN_TEST(test_script_set_icon_color);
    RUN_TEST(test_script_set_on_the_style_of_a_kind);
    RUN_TEST(test_changed_styles_reach_their_users);
    RUN_TEST(test_list_add_then_remove_gives_the_list_back);
    RUN_TEST(test_removed_keys);
    RUN_TEST(test_removed_keys_are_numbered_once_per_name);
    return UNITY_END();
}
