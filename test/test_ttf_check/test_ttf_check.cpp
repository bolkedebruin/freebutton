//
// Native tests for ttf_check (src/ui/fonts/ttf_check.c): the committed fonts
// pass, and damaged copies of them (truncated, bad offsets, wrong format) are
// rejected before stb_truetype would read past the file.
//
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/ui/fonts/ttf_check.c"

void setUp() {}
void tearDown() {}

// __FILE__ is absolute or relative to the project dir (the test's cwd)
static std::string repo_path(const char* rel) {
    std::string here = __FILE__;
    size_t at = here.rfind("test/test_ttf_check/");
    return (at == std::string::npos ? std::string() : here.substr(0, at)) + rel;
}

static std::vector<uint8_t> load(const char* rel) {
    std::vector<uint8_t> v;
    FILE* f = std::fopen(repo_path(rel).c_str(), "rb");
    if (!f) TEST_FAIL_MESSAGE(rel);
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
    std::fclose(f);
    if (v.size() < 1000) TEST_FAIL_MESSAGE(rel);  // not found: run from the project dir
    return v;
}

static uint16_t be16(const std::vector<uint8_t>& v, size_t o) { return (uint16_t)((v[o] << 8) | v[o + 1]); }
static uint32_t be32(const std::vector<uint8_t>& v, size_t o) {
    return ((uint32_t)v[o] << 24) | ((uint32_t)v[o + 1] << 16) | ((uint32_t)v[o + 2] << 8) | v[o + 3];
}
static void put16(std::vector<uint8_t>& v, size_t o, uint16_t x) {
    v[o] = (uint8_t)(x >> 8);
    v[o + 1] = (uint8_t)x;
}
static void put32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    for (int i = 0; i < 4; i++) v[o + i] = (uint8_t)(x >> (24 - 8 * i));
}

// table directory record of `tag`, or 0
static size_t record(const std::vector<uint8_t>& v, const char* tag) {
    for (uint16_t i = 0; i < be16(v, 4); i++) {
        size_t rec = 12 + 16u * i;
        if (std::memcmp(&v[rec], tag, 4) == 0) return rec;
    }
    return 0;
}

static ttf_check_t check(const std::vector<uint8_t>& v) { return ttf_check(v.data(), v.size()); }

static void test_committed_fonts_pass() {
    for (const char* f : {"media/fonts/medium.ttf", "media/fonts/semibold.ttf", "media/fonts/icons.ttf"}) {
        std::vector<uint8_t> v = load(f);
        TEST_ASSERT_TRUE_MESSAGE(v.size() > 1000, f);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", ttf_check_str(check(v)), f);
    }
}

// The defaults are the upstream files, byte for byte (media/fonts/README.md)
static void test_committed_fonts_are_upstream() {
    TEST_ASSERT_TRUE(load("media/fonts/medium.ttf") == load("tools/fonts/Montserrat-Medium.ttf"));
    TEST_ASSERT_TRUE(load("media/fonts/semibold.ttf") == load("tools/fonts/Montserrat-SemiBold.ttf"));
    TEST_ASSERT_TRUE(load("media/fonts/icons.ttf") == load("tools/fonts/materialdesignicons-webfont.ttf"));
}

// stb_truetype uses the LAST Unicode cmap record (platform 0, or 3 with
// encoding 1/10). MDI's codepoints are U+F0001+, so in icons.ttf that record
// must be (3,10) format 12; a format 4 table would lose every icon.
static void test_icons_last_unicode_cmap_is_format12() {
    std::vector<uint8_t> v = load("media/fonts/icons.ttf");
    size_t cmap = be32(v, record(v, "cmap") + 8);
    size_t last = 0;
    uint16_t pid = 0, eid = 0;
    for (uint16_t i = 0; i < be16(v, cmap + 2); i++) {
        size_t r = cmap + 4 + 8u * i;
        uint16_t p = be16(v, r), e = be16(v, r + 2);
        if (p == 0 || (p == 3 && (e == 1 || e == 10))) {
            last = cmap + be32(v, r + 4);
            pid = p;
            eid = e;
        }
    }
    TEST_ASSERT_NOT_EQUAL(0, last);
    TEST_ASSERT_EQUAL(3, pid);
    TEST_ASSERT_EQUAL(10, eid);
    TEST_ASSERT_EQUAL(12, be16(v, last));
}

// unmodified upstream fonts (what a user might upload) pass too
static void test_source_fonts_pass() {
    for (const char* f : {"tools/fonts/Montserrat-Medium.ttf", "tools/fonts/Montserrat-SemiBold.ttf",
                          "tools/fonts/Montserrat-Regular.ttf", "tools/fonts/materialdesignicons-webfont.ttf"}) {
        std::vector<uint8_t> v = load(f);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", ttf_check_str(check(v)), f);
    }
}

static void test_short_and_garbage() {
    TEST_ASSERT_EQUAL(TTF_ERR_SHORT, ttf_check(nullptr, 0));
    const uint8_t zeros[64] = {0};
    TEST_ASSERT_EQUAL(TTF_ERR_MAGIC, ttf_check(zeros, sizeof zeros));
    std::vector<uint8_t> js(200, 'a');  // e.g. a script uploaded to the wrong bucket
    TEST_ASSERT_EQUAL(TTF_ERR_MAGIC, check(js));
    std::vector<uint8_t> otf = load("media/fonts/medium.ttf");
    std::memcpy(otf.data(), "OTTO", 4);  // CFF outlines
    TEST_ASSERT_EQUAL(TTF_ERR_MAGIC, check(otf));
}

// (the last 1-3 bytes may be table padding, so cut at least 4)
static void test_truncated() {
    std::vector<uint8_t> v = load("media/fonts/medium.ttf");
    for (size_t keep : {(size_t)12, (size_t)100, v.size() / 2, v.size() - 4}) {
        std::vector<uint8_t> t(v.begin(), v.begin() + (long)keep);
        TEST_ASSERT_NOT_EQUAL(TTF_OK, check(t));
    }
}

static void test_table_outside_file() {
    std::vector<uint8_t> v = load("media/fonts/icons.ttf");
    size_t rec = record(v, "glyf");
    TEST_ASSERT_NOT_EQUAL(0, rec);
    put32(v, rec + 12, (uint32_t)v.size());  // glyf length = whole file
    TEST_ASSERT_EQUAL(TTF_ERR_TABLES, check(v));
}

static void test_missing_table() {
    std::vector<uint8_t> v = load("media/fonts/medium.ttf");
    std::memcpy(&v[record(v, "loca")], "xxxx", 4);
    TEST_ASSERT_EQUAL(TTF_ERR_MISSING, check(v));
}

static void test_glyph_count_too_large() {
    std::vector<uint8_t> v = load("media/fonts/medium.ttf");
    size_t maxp = be32(v, record(v, "maxp") + 8);
    put16(v, maxp + 4, 0xFFFF);  // hmtx and loca too short for it
    TEST_ASSERT_NOT_EQUAL(TTF_OK, check(v));
}

static void test_loca_outside_glyf() {
    std::vector<uint8_t> v = load("media/fonts/medium.ttf");
    size_t head = be32(v, record(v, "head") + 8);
    size_t loca = be32(v, record(v, "loca") + 8);
    size_t glyf_len = be32(v, record(v, "glyf") + 12);
    if (be16(v, head + 50)) {
        put32(v, loca + 8, (uint32_t)glyf_len + 4);
    } else {
        put16(v, loca + 4, (uint16_t)(glyf_len / 2 + 2));
    }
    TEST_ASSERT_EQUAL(TTF_ERR_LOCA, check(v));
}

static void test_cmap_glyph_out_of_range() {
    // icons.ttf: stb uses the (3,10) format 12 table; point a group past numGlyphs
    std::vector<uint8_t> v = load("media/fonts/icons.ttf");
    size_t cmap = be32(v, record(v, "cmap") + 8);
    size_t sub = 0;
    for (uint16_t i = 0; i < be16(v, cmap + 2); i++) {
        size_t r = cmap + 4 + 8u * i;
        if (be16(v, r) == 3 && be16(v, r + 2) == 10) sub = cmap + be32(v, r + 4);
    }
    TEST_ASSERT_NOT_EQUAL(0, sub);
    TEST_ASSERT_EQUAL(12, be16(v, sub));
    put32(v, sub + 16 + 8, 0xFFFF0);  // first group's start glyph
    TEST_ASSERT_EQUAL(TTF_ERR_CMAP, check(v));
}

static void test_cmap_format4_header() {
    // medium.ttf: (3,1) format 4; a wrong searchRange sends stb's search astray
    std::vector<uint8_t> v = load("media/fonts/medium.ttf");
    size_t cmap = be32(v, record(v, "cmap") + 8);
    size_t sub = 0;
    for (uint16_t i = 0; i < be16(v, cmap + 2); i++) {
        size_t r = cmap + 4 + 8u * i;
        if (be16(v, r) == 3 && be16(v, r + 2) == 1) sub = cmap + be32(v, r + 4);
    }
    TEST_ASSERT_EQUAL(4, be16(v, sub));
    put16(v, sub + 8, (uint16_t)(be16(v, sub + 8) * 4));
    TEST_ASSERT_EQUAL(TTF_ERR_CMAP, check(v));
}

static void test_gpos_lookup_outside() {
    std::vector<uint8_t> v = load("media/fonts/medium.ttf");
    size_t gpos = be32(v, record(v, "GPOS") + 8);
    size_t ll = gpos + be16(v, gpos + 8);
    put16(v, ll, 0xFFFF);  // lookup count: the offset array runs past the file
    TEST_ASSERT_EQUAL(TTF_ERR_GPOS, check(v));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_committed_fonts_pass);
    RUN_TEST(test_source_fonts_pass);
    RUN_TEST(test_committed_fonts_are_upstream);
    RUN_TEST(test_icons_last_unicode_cmap_is_format12);
    RUN_TEST(test_short_and_garbage);
    RUN_TEST(test_truncated);
    RUN_TEST(test_table_outside_file);
    RUN_TEST(test_missing_table);
    RUN_TEST(test_glyph_count_too_large);
    RUN_TEST(test_loca_outside_glyf);
    RUN_TEST(test_cmap_glyph_out_of_range);
    RUN_TEST(test_cmap_format4_header);
    RUN_TEST(test_gpos_lookup_outside);
    return UNITY_END();
}
