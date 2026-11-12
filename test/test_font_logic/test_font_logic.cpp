//
// Native tests for the font registry's pure rules (src/ui/fonts/font_logic.c):
// config entry validation, the face table merge (the two system faces +
// config), fallback resolution with the cycle / depth cut (explicit
// fallbacks only), "fontsize" -> px, the instance budget and the line box.
//
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/ui/fonts/font_logic.c"

void setUp() {}
void tearDown() {}

// ---- helpers ------------------------------------------------------------------

struct Warn {
    font_warn_t code;
    std::string name;
};

static void collect(void* ctx, font_warn_t code, const char* name) {
    static_cast<std::vector<Warn>*>(ctx)->push_back({code, name ? name : ""});
}

static int count(const std::vector<Warn>& w, font_warn_t code) {
    int n = 0;
    for (const Warn& x : w) n += x.code == code;
    return n;
}

static font_def_t def(const char* name, const char* src, int32_t id = 0) {
    font_def_t d;
    std::memset(&d, 0, sizeof(d));
    std::strncpy(d.name, name, sizeof(d.name) - 1);
    std::strncpy(d.src, src, sizeof(d.src) - 1);
    d.id = id;
    d.fallback_kind = FONT_FALLBACK_DEFAULT;
    return d;
}

static font_def_t fb_name(font_def_t d, const char* fallback) {
    d.fallback_kind = FONT_FALLBACK_NAME;
    std::strncpy(d.fallback_name, fallback, sizeof(d.fallback_name) - 1);
    return d;
}

static font_def_t fb_id(font_def_t d, int32_t id) {
    d.fallback_kind = FONT_FALLBACK_ID;
    d.fallback_id = id;
    return d;
}

static font_def_t fb_none(font_def_t d) {
    d.fallback_kind = FONT_FALLBACK_NONE;
    return d;
}

// A face table from config entries (merge + resolve), like fonts_configure.
struct Table {
    std::vector<font_def_t> defs;  // the specs point into it
    font_face_spec_t faces[FONTS_MAX_FACES];
    size_t count = 0;
    size_t accepted = 0;
    std::vector<Warn> warns;
    int8_t face_of[16];

    explicit Table(const std::vector<font_def_t>& d, size_t room = 512) : defs(d) {
        count = font_logic_builtins(faces);
        accepted = font_logic_merge(faces, &count, FONTS_MAX_FACES, defs.data(), defs.size(), room, face_of,
                                    collect, &warns);
        font_logic_resolve(faces, count, collect, &warns);
    }
    int find(const char* name) const { return font_logic_find(faces, count, name, std::strlen(name)); }
    int fallback(const char* name) const { return faces[find(name)].fallback; }
    int chain_len(int f) const {
        int n = 0;
        for (; f >= 0 && n <= FONTS_MAX_FACES; f = faces[f].fallback) n++;
        return n;
    }
};

// ---- validation ---------------------------------------------------------------

static bool vname(const char* s) { return font_logic_valid_name(s, std::strlen(s)); }

static void test_valid_names() {
    TEST_ASSERT_TRUE(vname("roboto"));
    TEST_ASSERT_TRUE(vname("_mono2"));
    TEST_ASSERT_TRUE(vname("A"));
    TEST_ASSERT_TRUE(vname("abcdefghijklmnopqrstuvwx"));    // 24
    TEST_ASSERT_FALSE(vname("abcdefghijklmnopqrstuvwxy"));  // 25
    TEST_ASSERT_FALSE(vname(""));
    TEST_ASSERT_FALSE(vname("2fast"));
    TEST_ASSERT_FALSE(vname("a-b"));
    TEST_ASSERT_FALSE(vname("a b"));
    TEST_ASSERT_FALSE(vname("a.ttf"));
    TEST_ASSERT_FALSE(vname("caf\xc3\xa9"));
    TEST_ASSERT_FALSE(font_logic_valid_name(nullptr, 3));
    TEST_ASSERT_TRUE(font_logic_valid_name("monoXYZ", 4));  // length-bounded
}

static void test_valid_srcs() {
    TEST_ASSERT_TRUE(font_logic_valid_src("roboto.ttf"));
    TEST_ASSERT_TRUE(font_logic_valid_src("Inter-Medium_v4.0.otf"));
    TEST_ASSERT_TRUE(font_logic_valid_src("a.ttf"));
    TEST_ASSERT_FALSE(font_logic_valid_src(".ttf"));
    TEST_ASSERT_FALSE(font_logic_valid_src(""));
    TEST_ASSERT_FALSE(font_logic_valid_src(nullptr));
    TEST_ASSERT_FALSE(font_logic_valid_src("roboto.woff2"));
    TEST_ASSERT_FALSE(font_logic_valid_src("roboto.TTF"));
    TEST_ASSERT_FALSE(font_logic_valid_src("roboto"));
    TEST_ASSERT_FALSE(font_logic_valid_src("dir/roboto.ttf"));
    TEST_ASSERT_FALSE(font_logic_valid_src("my font.ttf"));
    std::string k(92, 'a');
    TEST_ASSERT_TRUE(font_logic_valid_src((k + ".ttf").c_str()));    // 96
    TEST_ASSERT_FALSE(font_logic_valid_src((k + "a.ttf").c_str()));  // 97
}

static void test_valid_lineheight() {
    TEST_ASSERT_TRUE(font_logic_valid_lineheight(0));
    TEST_ASSERT_TRUE(font_logic_valid_lineheight(80));
    TEST_ASSERT_TRUE(font_logic_valid_lineheight(200));
    TEST_ASSERT_FALSE(font_logic_valid_lineheight(79));
    TEST_ASSERT_FALSE(font_logic_valid_lineheight(201));
}

// ---- system faces and merge -------------------------------------------------------

static void test_system_faces_without_fallback() {
    Table t({});
    TEST_ASSERT_EQUAL(2, t.count);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, t.find("montserrat"));
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, t.find("montserrat_semibold"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.find("medium"));  // the old built-in names are gone
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.find("semibold"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.find("icons"));
    TEST_ASSERT_EQUAL_STRING("medium.ttf", t.faces[FONT_FACE_SYSTEM].src);
    TEST_ASSERT_EQUAL_STRING("semibold.ttf", t.faces[FONT_FACE_SYSTEM_BOLD].src);
    // explicit fallbacks only
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.faces[FONT_FACE_SYSTEM].fallback);
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.faces[FONT_FACE_SYSTEM_BOLD].fallback);
    TEST_ASSERT_EQUAL(0, (int)t.warns.size());
}

static void test_config_faces_and_lookup() {
    Table t({fb_name(def("roboto", "roboto.ttf", 1), "mdi"), def("mono", "jetbrains.ttf", 2),
             fb_none(def("plain", "plain.otf")), def("mdi", "icons.ttf")});
    TEST_ASSERT_EQUAL(4, t.accepted);
    TEST_ASSERT_EQUAL(6, t.count);
    TEST_ASSERT_EQUAL(2, t.find("roboto"));
    TEST_ASSERT_EQUAL(3, t.find("mono"));
    TEST_ASSERT_EQUAL(4, t.find("plain"));
    TEST_ASSERT_EQUAL(5, t.find("mdi"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.find("nope"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, font_logic_find(t.faces, t.count, "mon", 3));
    TEST_ASSERT_EQUAL(3, font_logic_find(t.faces, t.count, "monoXX", 4));
    TEST_ASSERT_EQUAL(2, font_logic_find_id(t.faces, t.count, 1));
    TEST_ASSERT_EQUAL(3, font_logic_find_id(t.faces, t.count, 2));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, font_logic_find_id(t.faces, t.count, 0));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, font_logic_find_id(t.faces, t.count, 3));
    TEST_ASSERT_EQUAL(t.find("mdi"), t.fallback("roboto"));  // forward reference
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("mono"));   // "fallback" absent: none
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("plain"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("mdi"));
    TEST_ASSERT_EQUAL(2, t.face_of[0]);
    TEST_ASSERT_EQUAL(4, t.face_of[2]);
    TEST_ASSERT_EQUAL_STRING("icons.ttf", t.faces[5].src);
    TEST_ASSERT_EQUAL(0, (int)t.warns.size());
}

static void test_no_name_is_special() {
    // "icons" is an ordinary config face, and nothing falls back to it unless told
    Table t({def("icons", "icons.ttf"), def("roboto", "roboto.ttf")});
    TEST_ASSERT_EQUAL(2, t.accepted);
    TEST_ASSERT_EQUAL(2, t.find("icons"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("icons"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("roboto"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("montserrat"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("montserrat_semibold"));
    TEST_ASSERT_EQUAL(0, (int)t.warns.size());
}

static void test_system_entry_sets_fallback_and_lineheight() {
    font_def_t m = fb_name(def("montserrat", "", 7), "mdi");
    m.lineheight_x100 = 120;
    Table t({m, fb_id(def("montserrat_semibold", ""), 8), def("mdi", "icons.ttf", 8)});
    TEST_ASSERT_EQUAL(3, t.accepted);
    TEST_ASSERT_EQUAL(3, t.count);  // the system entries add no face
    TEST_ASSERT_EQUAL_STRING("medium.ttf", t.faces[FONT_FACE_SYSTEM].src);
    TEST_ASSERT_EQUAL(7, t.faces[FONT_FACE_SYSTEM].id);
    TEST_ASSERT_EQUAL(120, t.faces[FONT_FACE_SYSTEM].lineheight_x100);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, font_logic_find_id(t.faces, t.count, 7));
    TEST_ASSERT_EQUAL(2, t.fallback("montserrat"));
    TEST_ASSERT_EQUAL(2, t.fallback("montserrat_semibold"));
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM, t.face_of[0]);
    TEST_ASSERT_EQUAL(FONT_FACE_SYSTEM_BOLD, t.face_of[1]);
    TEST_ASSERT_EQUAL(0, (int)t.warns.size());
}

static void test_system_entry_src_ignored() {
    // a src on a system name: warned, ignored, the entry is kept
    Table t({fb_name(def("montserrat", "inter.ttf"), "mdi"), def("montserrat_semibold", "bad.woff2"),
             def("mdi", "icons.ttf")});
    TEST_ASSERT_EQUAL(3, t.accepted);
    TEST_ASSERT_EQUAL(2, count(t.warns, FONT_WARN_SYSTEM_SRC));
    TEST_ASSERT_EQUAL(0, count(t.warns, FONT_WARN_BAD_SRC));
    TEST_ASSERT_EQUAL((int)t.warns.size(), count(t.warns, FONT_WARN_SYSTEM_SRC));
    TEST_ASSERT_EQUAL_STRING("medium.ttf", t.faces[FONT_FACE_SYSTEM].src);
    TEST_ASSERT_EQUAL_STRING("semibold.ttf", t.faces[FONT_FACE_SYSTEM_BOLD].src);
    TEST_ASSERT_EQUAL(t.find("mdi"), t.fallback("montserrat"));
}

static void test_system_entry_only_once() {
    Table t({fb_none(def("montserrat_semibold", "", 3)), def("montserrat_semibold", "", 4)});
    TEST_ASSERT_EQUAL(1, t.accepted);
    TEST_ASSERT_EQUAL(3, t.faces[FONT_FACE_SYSTEM_BOLD].id);
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_DUP_NAME));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.face_of[1]);
}

static void test_duplicates_and_invalid_entries() {
    Table t({def("roboto", "roboto.ttf", 1), def("roboto", "other.ttf", 2), def("other", "other.ttf", 1),
             def("2bad", "x.ttf"), def("neg", "x.ttf", -1), def("nosrc", ""), def("badsrc", "x.woff2"),
             def("ok", "ok.ttf", 2)});
    TEST_ASSERT_EQUAL(2, t.accepted);
    TEST_ASSERT_EQUAL(4, t.count);
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_DUP_NAME));
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_DUP_ID));
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_BAD_NAME));
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_BAD_ID));
    TEST_ASSERT_EQUAL(2, count(t.warns, FONT_WARN_BAD_SRC));
    TEST_ASSERT_EQUAL(2, t.find("roboto"));
    TEST_ASSERT_EQUAL(3, t.find("ok"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.find("other"));
    TEST_ASSERT_EQUAL_STRING("roboto.ttf", t.faces[2].src);
}

static void test_duplicate_id_with_system_entry() {
    Table t({def("montserrat", "", 5), def("x", "x.ttf", 5)});
    TEST_ASSERT_EQUAL(1, t.accepted);
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_DUP_ID));
}

static void test_bad_lineheight_ignored() {
    font_def_t d = def("tall", "tall.ttf");
    d.lineheight_x100 = 250;
    Table t({d});
    TEST_ASSERT_EQUAL(1, t.accepted);
    TEST_ASSERT_EQUAL(0, t.faces[2].lineheight_x100);
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_BAD_LINEHEIGHT));
}

static void test_too_many_faces() {
    std::vector<font_def_t> defs;
    for (int i = 0; i < 8; i++) {
        char n[8];
        std::snprintf(n, sizeof n, "f%d", i);
        defs.push_back(def(n, "f.ttf"));
    }
    defs.push_back(fb_name(def("montserrat", ""), "f0"));  // a system entry needs no new slot
    Table t(defs);
    TEST_ASSERT_EQUAL(FONTS_MAX_FACES, t.count);
    TEST_ASSERT_EQUAL(7, t.accepted);  // 6 new + the system entry
    TEST_ASSERT_EQUAL(2, count(t.warns, FONT_WARN_TOO_MANY));
    TEST_ASSERT_EQUAL(t.find("f0"), t.fallback("montserrat"));
}

static void test_string_room() {
    // "aaaa" + "aaaa.ttf" = 5 + 9 = 14 bytes; room for two such entries
    const font_def_t a = def("aaaa", "aaaa.ttf"), keep = def("montserrat", ""), repl = def("montserrat", "m.ttf");
    TEST_ASSERT_EQUAL(14, font_logic_string_cost(&a, false));
    TEST_ASSERT_EQUAL(0, font_logic_string_cost(&keep, true));
    TEST_ASSERT_EQUAL(0, font_logic_string_cost(&repl, true));  // the src is ignored
    Table t({def("aaaa", "aaaa.ttf"), def("bbbb", "bbbb.ttf"), def("cccc", "cccc.ttf"), def("montserrat", "m.ttf")},
            28);
    TEST_ASSERT_EQUAL(3, t.accepted);  // the system entry costs nothing
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_TOO_MANY));
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_SYSTEM_SRC));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.find("cccc"));
}

// ---- fallback resolution ------------------------------------------------------------

static void test_chain_by_name_and_id() {
    Table t({fb_name(def("a", "a.ttf", 10), "b"), fb_id(def("b", "b.ttf", 11), 12),
             fb_name(def("c", "c.ttf", 12), "mdi"), def("mdi", "icons.ttf")});
    TEST_ASSERT_EQUAL(0, (int)t.warns.size());
    TEST_ASSERT_EQUAL(t.find("b"), t.fallback("a"));  // forward reference
    TEST_ASSERT_EQUAL(t.find("c"), t.fallback("b"));
    TEST_ASSERT_EQUAL(t.find("mdi"), t.fallback("c"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("mdi"));
    TEST_ASSERT_EQUAL(4, t.chain_len(t.find("a")));  // a b c mdi: the maximum
}

static void test_unknown_fallback() {
    Table t({fb_name(def("a", "a.ttf"), "ghost"), fb_id(def("b", "b.ttf"), 99), fb_name(def("c", "c.ttf"), ""),
             fb_name(def("d", "d.ttf"), "icons")});  // no face is called icons by default
    TEST_ASSERT_EQUAL(4, t.accepted);
    TEST_ASSERT_EQUAL(4, count(t.warns, FONT_WARN_UNKNOWN_FALLBACK));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("a"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("b"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("c"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("d"));
}

static void test_fallback_to_skipped_entry_is_unknown() {
    Table t({fb_name(def("a", "a.ttf"), "bad"), def("bad", "bad.woff")});
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_BAD_SRC));
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_UNKNOWN_FALLBACK));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("a"));
}

static void test_self_cycle() {
    Table t({fb_name(def("a", "a.ttf"), "a")});
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_CYCLE));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("a"));
}

static void test_two_cycle() {
    Table t({fb_name(def("a", "a.ttf"), "b"), fb_name(def("b", "b.ttf"), "a")});
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_CYCLE));
    TEST_ASSERT_EQUAL(t.find("b"), t.fallback("a"));  // a keeps its fallback, b -> a is cut
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("b"));
}

static void test_cycle_through_system_face() {
    // montserrat -> a -> montserrat
    Table t({fb_name(def("a", "a.ttf"), "montserrat"), fb_name(def("montserrat", ""), "a")});
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_CYCLE));
    for (size_t f = 0; f < t.count; f++) TEST_ASSERT_TRUE(t.chain_len((int)f) <= FONT_CHAIN_MAX);
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("a"));  // the walk from montserrat cuts a's link
    TEST_ASSERT_EQUAL(t.find("a"), t.fallback("montserrat"));
}

static void test_depth_cut() {
    // a -> b -> c -> d -> e: 5 faces
    Table t({fb_name(def("a", "a.ttf"), "b"), fb_name(def("b", "b.ttf"), "c"), fb_name(def("c", "c.ttf"), "d"),
             fb_name(def("d", "d.ttf"), "e"), def("e", "e.ttf")});
    TEST_ASSERT_EQUAL(1, count(t.warns, FONT_WARN_TOO_DEEP));
    TEST_ASSERT_EQUAL(0, count(t.warns, FONT_WARN_CYCLE));
    for (size_t f = 0; f < t.count; f++) TEST_ASSERT_TRUE(t.chain_len((int)f) <= FONT_CHAIN_MAX);
    // a keeps its three nearest fallbacks
    TEST_ASSERT_EQUAL(t.find("b"), t.fallback("a"));
    TEST_ASSERT_EQUAL(t.find("c"), t.fallback("b"));
    TEST_ASSERT_EQUAL(t.find("d"), t.fallback("c"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("d"));
    TEST_ASSERT_EQUAL(FONT_FACE_NONE, t.fallback("e"));
}

static void test_every_chain_bounded_and_acyclic() {
    // a dense tangle: every face points at the next, the last back at the first
    Table t({fb_name(def("a", "a.ttf"), "b"), fb_name(def("b", "b.ttf"), "c"), fb_name(def("c", "c.ttf"), "d"),
             fb_name(def("d", "d.ttf"), "e"), fb_name(def("e", "e.ttf"), "a"), fb_name(def("montserrat", ""), "a"),
             fb_name(def("montserrat_semibold", ""), "montserrat_semibold")});
    for (size_t f = 0; f < t.count; f++) TEST_ASSERT_TRUE(t.chain_len((int)f) <= FONT_CHAIN_MAX);
}

// ---- sizes ------------------------------------------------------------------------

static void test_px_mapping() {
    TEST_ASSERT_EQUAL(14, font_logic_px(1, 99));
    TEST_ASSERT_EQUAL(24, font_logic_px(2, 99));
    TEST_ASSERT_EQUAL(32, font_logic_px(3, 99));
    TEST_ASSERT_EQUAL(48, font_logic_px(4, 99));
    TEST_ASSERT_EQUAL(72, font_logic_px(5, 99));
    TEST_ASSERT_EQUAL(6, font_logic_px(6, 99));
    TEST_ASSERT_EQUAL(40, font_logic_px(40, 99));
    TEST_ASSERT_EQUAL(72, font_logic_px(72, 99));
    TEST_ASSERT_EQUAL(32, font_logic_px(0, 32));
    TEST_ASSERT_EQUAL(32, font_logic_px(73, 32));
    TEST_ASSERT_EQUAL(32, font_logic_px(-1, 32));
}

// ---- budget -------------------------------------------------------------------------

static void test_budget() {
    TEST_ASSERT_EQUAL(FONT_BUDGET_OK, font_logic_budget(0, 24, 12000, 8000));
    TEST_ASSERT_EQUAL(FONT_BUDGET_OK, font_logic_budget(23, 24, 6144 + 512, 1024));
    TEST_ASSERT_EQUAL(FONT_BUDGET_TABLE_FULL, font_logic_budget(24, 24, 60000, 60000));
    TEST_ASSERT_EQUAL(FONT_BUDGET_POOL_LOW, font_logic_budget(3, 24, 6144 + 511, 4000));
    TEST_ASSERT_EQUAL(FONT_BUDGET_POOL_LOW, font_logic_budget(3, 24, 20000, 1023));
    TEST_ASSERT_EQUAL(FONT_BUDGET_OK, font_logic_budget(0, 1, 7000, 2000));  // reload: slot exists
    // .81 after 9 extra sizes: the pool at 5.4 KB refuses the 10th (built-in font) instead of
    // leaving existing text without room
    TEST_ASSERT_EQUAL(FONT_BUDGET_POOL_LOW, font_logic_budget(9, 24, 5400, 3000));
}

static void test_glyph_scratch() {
    TEST_ASSERT_EQUAL(5120u + 14u * 14u * 3u / 2u, font_logic_glyph_scratch(14, 14));
    TEST_ASSERT_EQUAL(5120u + 7776u, font_logic_glyph_scratch(72, 72));
    // 48 px icon: 8576 B scratch + 16 KB reserve
    TEST_ASSERT_TRUE(font_logic_glyph_fits(48, 48, 8576 + 16384, 4096));
    TEST_ASSERT_FALSE(font_logic_glyph_fits(48, 48, 8576 + 16383, 30000));
    TEST_ASSERT_FALSE(font_logic_glyph_fits(14, 14, 60000, 4095));  // fragmented heap
    TEST_ASSERT_TRUE(font_logic_glyph_fits(72, 72, 58000, 20000));  // .81's usual heap: every size
}

// ---- line box -----------------------------------------------------------------------

static void test_line_box_legacy_medium() {
    // Montserrat at 48 px from tiny_ttf: ~58 / ~12
    int32_t lh = 58, base = 12;
    font_logic_line_box(48, 0, true, &lh, &base);
    TEST_ASSERT_EQUAL(49, lh);   // 48 * 1.03 = 49.44
    TEST_ASSERT_EQUAL(7, base);  // 48 * 0.155 = 7.44
    lh = 88, base = 18;
    font_logic_line_box(72, 0, true, &lh, &base);
    TEST_ASSERT_EQUAL(74, lh);    // 74.16
    TEST_ASSERT_EQUAL(11, base);  // 11.16
    lh = 39, base = 8;
    font_logic_line_box(32, 0, true, &lh, &base);  // below 48: font metrics
    TEST_ASSERT_EQUAL(39, lh);
    TEST_ASSERT_EQUAL(8, base);
    lh = 58, base = 12;
    font_logic_line_box(48, 0, false, &lh, &base);  // faces other than FONT_FACE_SYSTEM: font metrics
    TEST_ASSERT_EQUAL(58, lh);
    TEST_ASSERT_EQUAL(12, base);
}

static void test_line_box_config_lineheight() {
    int32_t lh = 29, base = 6;  // 24 px, 1.22 em
    font_logic_line_box(24, 150, false, &lh, &base);
    TEST_ASSERT_EQUAL(36, lh);
    TEST_ASSERT_EQUAL(9, base);  // half of the 7 px extra below: 6 + 3
    lh = 58, base = 12;
    font_logic_line_box(48, 120, true, &lh, &base);  // a config lineheight beats the legacy box
    TEST_ASSERT_EQUAL(58, lh);                       // 57.6
    TEST_ASSERT_EQUAL(12, base);
    lh = 29, base = 6;
    font_logic_line_box(24, 80, false, &lh, &base);  // tighter: 19 px, 10 less, 5 off the bottom
    TEST_ASSERT_EQUAL(19, lh);
    TEST_ASSERT_EQUAL(1, base);
    lh = 20, base = 2;
    font_logic_line_box(10, 80, false, &lh, &base);  // clamped at 0
    TEST_ASSERT_EQUAL(8, lh);
    TEST_ASSERT_EQUAL(0, base);
}

static void test_warn_strings() {
    for (int c = 0; c < FONT_WARN_COUNT; c++) {
        TEST_ASSERT_NOT_EQUAL(0, (int)std::strlen(font_logic_warn_str((font_warn_t)c)));
        TEST_ASSERT_NOT_EQUAL(0, std::strcmp("?", font_logic_warn_str((font_warn_t)c)));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_names);
    RUN_TEST(test_valid_srcs);
    RUN_TEST(test_valid_lineheight);
    RUN_TEST(test_system_faces_without_fallback);
    RUN_TEST(test_config_faces_and_lookup);
    RUN_TEST(test_no_name_is_special);
    RUN_TEST(test_system_entry_sets_fallback_and_lineheight);
    RUN_TEST(test_system_entry_src_ignored);
    RUN_TEST(test_system_entry_only_once);
    RUN_TEST(test_duplicates_and_invalid_entries);
    RUN_TEST(test_duplicate_id_with_system_entry);
    RUN_TEST(test_bad_lineheight_ignored);
    RUN_TEST(test_too_many_faces);
    RUN_TEST(test_string_room);
    RUN_TEST(test_chain_by_name_and_id);
    RUN_TEST(test_unknown_fallback);
    RUN_TEST(test_fallback_to_skipped_entry_is_unknown);
    RUN_TEST(test_self_cycle);
    RUN_TEST(test_two_cycle);
    RUN_TEST(test_cycle_through_system_face);
    RUN_TEST(test_depth_cut);
    RUN_TEST(test_every_chain_bounded_and_acyclic);
    RUN_TEST(test_px_mapping);
    RUN_TEST(test_budget);
    RUN_TEST(test_line_box_legacy_medium);
    RUN_TEST(test_line_box_config_lineheight);
    RUN_TEST(test_warn_strings);
    RUN_TEST(test_glyph_scratch);
    return UNITY_END();
}
