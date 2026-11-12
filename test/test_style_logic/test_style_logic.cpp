//
// Native tests for the config.json "style" cascade (src/ui/style_logic.c):
// the defaults, role -> element resolution, the display.unit and
// list.selected rules, the item-layer size and property parsing. The web
// IDE's style-config.ts follows the same rules (same vectors).
//
#include <unity.h>

#include <cstring>

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

static style_sheet_t empty_sheet() {
    style_sheet_t s;
    std::memset(&s, 0, sizeof(s));
    return s;
}

static void assert_text(const style_props_t& p, uint16_t has, int face, int px, uint32_t color) {
    TEST_ASSERT_EQUAL_HEX16(has, p.has);
    if (has & STYLE_HAS_FONT) TEST_ASSERT_EQUAL(face, p.face);
    if (has & STYLE_HAS_SIZE) TEST_ASSERT_EQUAL(px, p.px);
    if (has & STYLE_HAS_COLOR) TEST_ASSERT_EQUAL_HEX32(color, p.color);
}

// Field by field (struct copies need not copy the padding).
static void assert_same(const style_props_t& a, const style_props_t& b) {
    TEST_ASSERT_EQUAL_HEX16(a.has, b.has);
    TEST_ASSERT_EQUAL(a.face, b.face);
    TEST_ASSERT_EQUAL(a.px, b.px);
    TEST_ASSERT_EQUAL(a.opa, b.opa);
    TEST_ASSERT_EQUAL(a.align, b.align);
    TEST_ASSERT_EQUAL(a.decor, b.decor);
    TEST_ASSERT_EQUAL(a.letter_space, b.letter_space);
    TEST_ASSERT_EQUAL(a.line_space, b.line_space);
    TEST_ASSERT_EQUAL(a.bg_opa, b.bg_opa);
    TEST_ASSERT_EQUAL_HEX32(a.color, b.color);
    TEST_ASSERT_EQUAL_HEX32(a.bg_color, b.bg_color);
}

static const uint16_t kText = STYLE_HAS_FONT | STYLE_HAS_SIZE | STYLE_HAS_COLOR;
static const uint16_t kBg = STYLE_HAS_BG_COLOR | STYLE_HAS_BG_OPA;

// ---- defaults ------------------------------------------------------------------

static void test_defaults_table() {
    style_props_t d[STYLE_EL_COUNT];
    std::memset(d, 0xAB, sizeof(d));
    style_logic_defaults(d);
    assert_text(d[STYLE_EL_KEY_LABEL], kText, FONT_FACE_SYSTEM, 32, 0xFFFFFF);
    assert_text(d[STYLE_EL_KEY_TOPLABEL], kText, FONT_FACE_SYSTEM_BOLD, 18, 0xFFD700);
    assert_text(d[STYLE_EL_DISPLAY_VALUE], kText, FONT_FACE_SYSTEM, 14, 0xFFFFFF);
    assert_text(d[STYLE_EL_DISPLAY_UNIT], STYLE_HAS_COLOR, 0, 0, 0x999999);
    assert_text(d[STYLE_EL_DISPLAY_CAPTION], kText, FONT_FACE_SYSTEM_BOLD, 14, 0xFFD700);
    assert_text(d[STYLE_EL_LIST], kText, FONT_FACE_SYSTEM, 32, 0x808080);
    const style_props_t& s = d[STYLE_EL_LIST_SELECTED];
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR | kBg, s.has);
    TEST_ASSERT_EQUAL_HEX32(0x000000, s.color);
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, s.bg_color);
    TEST_ASSERT_EQUAL(255, s.bg_opa);
}

static void test_resolve_without_style() {
    style_props_t d[STYLE_EL_COUNT], r[STYLE_EL_COUNT];
    style_logic_defaults(d);
    style_logic_resolve(nullptr, r);
    for (int el = 0; el < STYLE_EL_LIST_SELECTED; el++) {
        assert_same(d[el], r[el]);
    }
    // the selected row: the list's font and size, its own colors
    const style_props_t& s = r[STYLE_EL_LIST_SELECTED];
    TEST_ASSERT_EQUAL_HEX16(kText | kBg, s.has);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, s.face);
    TEST_ASSERT_EQUAL(32, s.px);
    TEST_ASSERT_EQUAL_HEX32(0x000000, s.color);
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, s.bg_color);
    TEST_ASSERT_EQUAL(255, s.bg_opa);
    // an empty sheet resolves the same
    style_sheet_t e = empty_sheet();
    style_props_t r2[STYLE_EL_COUNT];
    style_logic_resolve(&e, r2);
    for (int el = 0; el < STYLE_EL_COUNT; el++) assert_same(r[el], r2[el]);
}

static void test_roles_of_elements() {
    TEST_ASSERT_EQUAL(STYLE_ROLE_TEXT, style_logic_role_of(STYLE_EL_KEY_LABEL));
    TEST_ASSERT_EQUAL(STYLE_ROLE_TITLE, style_logic_role_of(STYLE_EL_KEY_TOPLABEL));
    TEST_ASSERT_EQUAL(STYLE_ROLE_TEXT, style_logic_role_of(STYLE_EL_DISPLAY_VALUE));
    TEST_ASSERT_EQUAL(STYLE_ROLE_TEXT, style_logic_role_of(STYLE_EL_DISPLAY_UNIT));
    TEST_ASSERT_EQUAL(STYLE_ROLE_TITLE, style_logic_role_of(STYLE_EL_DISPLAY_CAPTION));
    TEST_ASSERT_EQUAL(STYLE_ROLE_TEXT, style_logic_role_of(STYLE_EL_LIST));
    TEST_ASSERT_EQUAL(STYLE_ROLE_TEXT, style_logic_role_of(STYLE_EL_LIST_SELECTED));
}

// ---- cascade --------------------------------------------------------------------

static void test_role_then_element() {
    style_sheet_t s = empty_sheet();
    s.role[STYLE_ROLE_TEXT].has = STYLE_HAS_COLOR | STYLE_HAS_LETTER_SPACE;
    s.role[STYLE_ROLE_TEXT].color = 0x00FF00;
    s.role[STYLE_ROLE_TEXT].letter_space = 3;
    s.element[STYLE_EL_KEY_LABEL].has = STYLE_HAS_COLOR;
    s.element[STYLE_EL_KEY_LABEL].color = 0xFF0000;
    style_props_t r[STYLE_EL_COUNT];
    style_logic_resolve(&s, r);
    // the element beats the role; the role's other properties stay
    assert_text(r[STYLE_EL_KEY_LABEL], kText | STYLE_HAS_LETTER_SPACE, FONT_FACE_SYSTEM, 32, 0xFF0000);
    TEST_ASSERT_EQUAL(3, r[STYLE_EL_KEY_LABEL].letter_space);
    // the role beats the defaults on every "text" element
    assert_text(r[STYLE_EL_DISPLAY_VALUE], kText | STYLE_HAS_LETTER_SPACE, FONT_FACE_SYSTEM, 14, 0x00FF00);
    assert_text(r[STYLE_EL_LIST], kText | STYLE_HAS_LETTER_SPACE, FONT_FACE_SYSTEM, 32, 0x00FF00);
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, r[STYLE_EL_DISPLAY_UNIT].color);  // color cascades to the unit
    // "title" elements are untouched
    assert_text(r[STYLE_EL_KEY_TOPLABEL], kText, FONT_FACE_SYSTEM_BOLD, 18, 0xFFD700);
    assert_text(r[STYLE_EL_DISPLAY_CAPTION], kText, FONT_FACE_SYSTEM_BOLD, 14, 0xFFD700);
}

static void test_font_from_role_size_from_element() {
    style_sheet_t s = empty_sheet();
    s.role[STYLE_ROLE_TITLE].has = STYLE_HAS_FONT;
    s.role[STYLE_ROLE_TITLE].face = 3;
    s.element[STYLE_EL_KEY_TOPLABEL].has = STYLE_HAS_SIZE;
    s.element[STYLE_EL_KEY_TOPLABEL].px = 24;
    style_props_t r[STYLE_EL_COUNT];
    style_logic_resolve(&s, r);
    assert_text(r[STYLE_EL_KEY_TOPLABEL], kText, 3, 24, 0xFFD700);
    assert_text(r[STYLE_EL_DISPLAY_CAPTION], kText, 3, 14, 0xFFD700);
    assert_text(r[STYLE_EL_KEY_LABEL], kText, FONT_FACE_SYSTEM, 32, 0xFFFFFF);
}

static void test_unit_font_only_from_its_element() {
    style_sheet_t s = empty_sheet();
    s.role[STYLE_ROLE_TEXT].has = STYLE_HAS_FONT | STYLE_HAS_SIZE | STYLE_HAS_ALIGN;
    s.role[STYLE_ROLE_TEXT].face = 3;
    s.role[STYLE_ROLE_TEXT].px = 20;
    s.role[STYLE_ROLE_TEXT].align = STYLE_ALIGN_RIGHT;
    style_props_t r[STYLE_EL_COUNT];
    style_logic_resolve(&s, r);
    // the role's font reaches the value, not the unit (it takes the value's)
    assert_text(r[STYLE_EL_DISPLAY_VALUE], kText | STYLE_HAS_ALIGN, 3, 20, 0xFFFFFF);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR | STYLE_HAS_ALIGN, r[STYLE_EL_DISPLAY_UNIT].has);
    TEST_ASSERT_EQUAL_HEX32(0x999999, r[STYLE_EL_DISPLAY_UNIT].color);
    TEST_ASSERT_EQUAL(STYLE_ALIGN_RIGHT, r[STYLE_EL_DISPLAY_UNIT].align);
    // the unit element sets a size of its own (and only that)
    s.element[STYLE_EL_DISPLAY_UNIT].has = STYLE_HAS_SIZE;
    s.element[STYLE_EL_DISPLAY_UNIT].px = 12;
    style_logic_resolve(&s, r);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_SIZE | STYLE_HAS_COLOR | STYLE_HAS_ALIGN, r[STYLE_EL_DISPLAY_UNIT].has);
    TEST_ASSERT_EQUAL(12, r[STYLE_EL_DISPLAY_UNIT].px);
    // and a font
    s.element[STYLE_EL_DISPLAY_UNIT].has |= STYLE_HAS_FONT;
    s.element[STYLE_EL_DISPLAY_UNIT].face = FONT_FACE_SYSTEM_BOLD;
    style_logic_resolve(&s, r);
    assert_text(r[STYLE_EL_DISPLAY_UNIT], kText | STYLE_HAS_ALIGN, FONT_FACE_SYSTEM_BOLD, 12, 0x999999);
}

static void test_list_selected_inherits_list() {
    style_sheet_t s = empty_sheet();
    style_props_t& l = s.element[STYLE_EL_LIST];
    l.has = STYLE_HAS_SIZE | STYLE_HAS_COLOR | STYLE_HAS_LETTER_SPACE;
    l.px = 24;
    l.color = 0x00FF00;
    l.letter_space = 2;
    s.role[STYLE_ROLE_TEXT].has = STYLE_HAS_FONT;
    s.role[STYLE_ROLE_TEXT].face = 3;
    style_props_t r[STYLE_EL_COUNT];
    style_logic_resolve(&s, r);
    assert_text(r[STYLE_EL_LIST], kText | STYLE_HAS_LETTER_SPACE, 3, 24, 0x00FF00);
    // the selected row: the list's text properties, its own color and bg
    const style_props_t* sel = &r[STYLE_EL_LIST_SELECTED];
    assert_text(*sel, kText | STYLE_HAS_LETTER_SPACE | kBg, 3, 24, 0x000000);
    TEST_ASSERT_EQUAL(2, sel->letter_space);
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, sel->bg_color);
    TEST_ASSERT_EQUAL(255, sel->bg_opa);
    // list.selected's own properties win
    style_props_t& o = s.element[STYLE_EL_LIST_SELECTED];
    o.has = STYLE_HAS_BG_COLOR | STYLE_HAS_SIZE | STYLE_HAS_COLOR;
    o.bg_color = 0x123456;
    o.px = 28;
    o.color = 0xFFFFFF;
    style_logic_resolve(&s, r);
    sel = &r[STYLE_EL_LIST_SELECTED];
    assert_text(*sel, kText | STYLE_HAS_LETTER_SPACE | kBg, 3, 28, 0xFFFFFF);
    TEST_ASSERT_EQUAL_HEX32(0x123456, sel->bg_color);
    TEST_ASSERT_EQUAL(255, sel->bg_opa);
    TEST_ASSERT_EQUAL(24, r[STYLE_EL_LIST].px);  // the list itself is unchanged
}

static void test_bg_stays_on_selected() {
    // a sheet filled by hand with bg on other layers: ignored there
    style_sheet_t s = empty_sheet();
    s.role[STYLE_ROLE_TEXT].has = STYLE_HAS_BG_COLOR;
    s.role[STYLE_ROLE_TEXT].bg_color = 0x111111;
    s.element[STYLE_EL_LIST].has = STYLE_HAS_BG_OPA;
    s.element[STYLE_EL_LIST].bg_opa = 10;
    style_props_t r[STYLE_EL_COUNT];
    style_logic_resolve(&s, r);
    TEST_ASSERT_EQUAL(0, r[STYLE_EL_KEY_LABEL].has & kBg);
    TEST_ASSERT_EQUAL(0, r[STYLE_EL_LIST].has & kBg);
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, r[STYLE_EL_LIST_SELECTED].bg_color);
    TEST_ASSERT_EQUAL(255, r[STYLE_EL_LIST_SELECTED].bg_opa);
}

static void test_merge() {
    style_props_t a, b;
    std::memset(&a, 0, sizeof(a));
    std::memset(&b, 0, sizeof(b));
    a.has = STYLE_HAS_COLOR | STYLE_HAS_SIZE;
    a.color = 1;
    a.px = 14;
    b.has = STYLE_HAS_COLOR | STYLE_HAS_DECOR | STYLE_HAS_LINE_SPACE | STYLE_HAS_OPA;
    b.color = 2;
    b.px = 40;  // not set in b: ignored
    b.decor = STYLE_DECOR_UNDERLINE;
    b.line_space = -4;
    b.opa = 128;
    style_logic_merge(&a, &b);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_COLOR | STYLE_HAS_SIZE | STYLE_HAS_DECOR | STYLE_HAS_LINE_SPACE | STYLE_HAS_OPA,
                            a.has);
    TEST_ASSERT_EQUAL(2, a.color);
    TEST_ASSERT_EQUAL(14, a.px);
    TEST_ASSERT_EQUAL(STYLE_DECOR_UNDERLINE, a.decor);
    TEST_ASSERT_EQUAL(-4, a.line_space);
    TEST_ASSERT_EQUAL(128, a.opa);
}

// ---- item layer -------------------------------------------------------------------

static void test_item_px() {
    TEST_ASSERT_EQUAL(14, style_logic_item_px(1, false, 99));
    TEST_ASSERT_EQUAL(24, style_logic_item_px(2, false, 99));
    TEST_ASSERT_EQUAL(18, style_logic_item_px(2, true, 99));  // a title's step 2
    TEST_ASSERT_EQUAL(14, style_logic_item_px(1, true, 99));
    TEST_ASSERT_EQUAL(32, style_logic_item_px(3, true, 99));
    TEST_ASSERT_EQUAL(48, style_logic_item_px(4, false, 99));
    TEST_ASSERT_EQUAL(72, style_logic_item_px(5, true, 99));
    TEST_ASSERT_EQUAL(6, style_logic_item_px(6, true, 99));
    TEST_ASSERT_EQUAL(40, style_logic_item_px(40, false, 99));
    TEST_ASSERT_EQUAL(72, style_logic_item_px(72, false, 99));
    TEST_ASSERT_EQUAL(18, style_logic_item_px(0, true, 18));
    TEST_ASSERT_EQUAL(32, style_logic_item_px(73, false, 32));
    TEST_ASSERT_EQUAL(32, style_logic_item_px(-1, false, 32));
}

// ---- lookup -------------------------------------------------------------------------

static void test_element_and_role_lookup() {
    TEST_ASSERT_EQUAL(STYLE_EL_KEY_LABEL, style_logic_element("key", "label"));
    TEST_ASSERT_EQUAL(STYLE_EL_KEY_TOPLABEL, style_logic_element("key", "toplabel"));
    TEST_ASSERT_EQUAL(STYLE_EL_DISPLAY_VALUE, style_logic_element("display", "value"));
    TEST_ASSERT_EQUAL(STYLE_EL_DISPLAY_UNIT, style_logic_element("display", "unit"));
    TEST_ASSERT_EQUAL(STYLE_EL_DISPLAY_CAPTION, style_logic_element("display", "caption"));
    TEST_ASSERT_EQUAL(STYLE_EL_LIST, style_logic_element("list", nullptr));
    TEST_ASSERT_EQUAL(STYLE_EL_LIST_SELECTED, style_logic_element("list", "selected"));
    TEST_ASSERT_EQUAL(-1, style_logic_element("key", nullptr));
    TEST_ASSERT_EQUAL(-1, style_logic_element("display", nullptr));
    TEST_ASSERT_EQUAL(-1, style_logic_element("key", "value"));
    TEST_ASSERT_EQUAL(-1, style_logic_element("list", "label"));
    TEST_ASSERT_EQUAL(-1, style_logic_element("button", "label"));
    TEST_ASSERT_EQUAL(-1, style_logic_element(nullptr, "label"));
    TEST_ASSERT_EQUAL(STYLE_ROLE_TEXT, style_logic_role("text"));
    TEST_ASSERT_EQUAL(STYLE_ROLE_TITLE, style_logic_role("title"));
    TEST_ASSERT_EQUAL(-1, style_logic_role("Text"));
    TEST_ASSERT_EQUAL(-1, style_logic_role(""));
    TEST_ASSERT_EQUAL(-1, style_logic_role(nullptr));
}

// ---- properties ----------------------------------------------------------------------

static style_props_t blank() {
    style_props_t p;
    std::memset(&p, 0, sizeof(p));
    return p;
}

static style_prop_status_t str(style_props_t* p, const char* prop, const char* v, bool bg = false) {
    return style_logic_set_string(p, prop, v, bg, resolver, nullptr);
}

static style_prop_status_t num(style_props_t* p, const char* prop, int32_t v, bool bg = false) {
    return style_logic_set_number(p, prop, v, bg, resolver, nullptr);
}

static void test_set_valid_properties() {
    style_props_t p = blank();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, str(&p, "text_font", "montserrat_semibold"));
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, p.face);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, num(&p, "text_font", 7));  // a config font id
    TEST_ASSERT_EQUAL(2, p.face);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, num(&p, "text_size", 6));
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, num(&p, "text_size", 72));
    TEST_ASSERT_EQUAL(72, p.px);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, str(&p, "text_color", "#ffd700"));
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, p.color);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, num(&p, "text_opa", 0));
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, num(&p, "text_opa", 255));
    TEST_ASSERT_EQUAL(255, p.opa);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, str(&p, "text_align", "left"));
    TEST_ASSERT_EQUAL(STYLE_ALIGN_LEFT, p.align);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, str(&p, "text_align", "right"));
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, str(&p, "text_align", "center"));
    TEST_ASSERT_EQUAL(STYLE_ALIGN_CENTER, p.align);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, num(&p, "text_letter_space", -100));
    TEST_ASSERT_EQUAL(-100, p.letter_space);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, num(&p, "text_line_space", 100));
    TEST_ASSERT_EQUAL(100, p.line_space);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, str(&p, "text_decor", "underline strikethrough"));
    TEST_ASSERT_EQUAL(STYLE_DECOR_UNDERLINE | STYLE_DECOR_STRIKETHROUGH, p.decor);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, str(&p, "text_decor", "strikethrough"));
    TEST_ASSERT_EQUAL(STYLE_DECOR_STRIKETHROUGH, p.decor);
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, str(&p, "text_decor", "none"));
    TEST_ASSERT_EQUAL(STYLE_DECOR_NONE, p.decor);
    TEST_ASSERT_EQUAL_HEX16(STYLE_HAS_FONT | STYLE_HAS_SIZE | STYLE_HAS_COLOR | STYLE_HAS_OPA | STYLE_HAS_ALIGN |
                                STYLE_HAS_LETTER_SPACE | STYLE_HAS_LINE_SPACE | STYLE_HAS_DECOR,
                            p.has);
    // bg on list.selected
    style_props_t s = blank();
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, str(&s, "bg_color", "000000", true));
    TEST_ASSERT_EQUAL(STYLE_PROP_OK, num(&s, "bg_opa", 128, true));
    TEST_ASSERT_EQUAL_HEX16(kBg, s.has);
    TEST_ASSERT_EQUAL(128, s.bg_opa);
}

static void test_set_errors_leave_props_unchanged() {
    style_props_t p = blank();
    p.has = STYLE_HAS_SIZE | STYLE_HAS_COLOR;
    p.px = 20;
    p.color = 0xABCDEF;
    const style_props_t before = p;
    // unknown property
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, str(&p, "color", "#ffffff"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, num(&p, "fontsize", 3));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, str(&p, "text_colour", "#ffffff"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, str(&p, nullptr, "#ffffff"));
    // bg outside list.selected
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, str(&p, "bg_color", "#ffffff"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN, num(&p, "bg_opa", 255));
    // bad colors
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "text_color", "#fff"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "text_color", "red"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "text_color", "#gg0000"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "bg_color", "#1234567", true));
    // sizes out of range
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "text_size", 5));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "text_size", 73));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "text_size", 0));
    // other ranges
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "text_opa", 256));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "text_opa", -1));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "bg_opa", 300, true));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "text_letter_space", 101));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "text_line_space", -101));
    // bad keywords
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "text_align", "middle"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "text_align", "Left"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "text_decor", "overline"));
    // wrong JSON type
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "text_size", "32"));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "text_color", 0xFFFFFF));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, num(&p, "text_align", 1));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "text_color", nullptr));
    // fonts
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN_FONT, str(&p, "text_font", "roboto"));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN_FONT, str(&p, "text_font", "medium"));  // the old built-in name
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN_FONT, num(&p, "text_font", 8));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN_FONT, num(&p, "text_font", 0));
    TEST_ASSERT_EQUAL(STYLE_PROP_BAD_VALUE, str(&p, "text_font", ""));
    TEST_ASSERT_EQUAL(STYLE_PROP_UNKNOWN_FONT,
                      style_logic_set_string(&p, "text_font", "montserrat", false, nullptr, nullptr));
    assert_same(before, p);
}

static void test_parse_color() {
    uint32_t c = 0;
    TEST_ASSERT_TRUE(style_logic_parse_color("#ffd700", &c));
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, c);
    TEST_ASSERT_TRUE(style_logic_parse_color("808080", &c));
    TEST_ASSERT_EQUAL_HEX32(0x808080, c);
    TEST_ASSERT_TRUE(style_logic_parse_color("ABCDEF", &c));
    TEST_ASSERT_EQUAL_HEX32(0xABCDEF, c);
    TEST_ASSERT_TRUE(style_logic_parse_color("#000000", &c));
    TEST_ASSERT_EQUAL_HEX32(0x000000, c);
    c = 0x42;
    TEST_ASSERT_FALSE(style_logic_parse_color("", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color("#", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color("#fff", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color("12345", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color("1234567", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color("##123456", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color("#12345g", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color(" 123456", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color("0x123456", &c));
    TEST_ASSERT_FALSE(style_logic_parse_color(nullptr, &c));
    TEST_ASSERT_EQUAL_HEX32(0x42, c);  // untouched on failure
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_table);
    RUN_TEST(test_resolve_without_style);
    RUN_TEST(test_roles_of_elements);
    RUN_TEST(test_role_then_element);
    RUN_TEST(test_font_from_role_size_from_element);
    RUN_TEST(test_unit_font_only_from_its_element);
    RUN_TEST(test_list_selected_inherits_list);
    RUN_TEST(test_bg_stays_on_selected);
    RUN_TEST(test_merge);
    RUN_TEST(test_item_px);
    RUN_TEST(test_element_and_role_lookup);
    RUN_TEST(test_set_valid_properties);
    RUN_TEST(test_set_errors_leave_props_unchanged);
    RUN_TEST(test_parse_color);
    return UNITY_END();
}
