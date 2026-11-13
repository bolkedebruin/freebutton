//
// Native tests for the display text markup parser (src/ui/text_spans.c):
// every escape, the malformed forms that stay literal, span merging and
// truncation. The web IDE's preview parser (web/src/js/text-spans.ts) follows
// the same rules.
//
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "../../src/ui/text_spans.c"
}

void setUp() {}
void tearDown() {}

namespace {

// Fake font table: "mono" = face 5, "roboto" = face 3, id 7 = face 4.
int resolver(const char* name, size_t len, bool is_id, int32_t id, void* ctx) {
    if (ctx != nullptr) {
        ++*static_cast<int*>(ctx);
    }
    const std::string n(name, len);
    if (is_id) {
        return id == 7 ? 4 : TEXT_FONT_UNKNOWN;
    }
    if (n == "mono") return 5;
    if (n == "roboto") return 3;
    return TEXT_FONT_UNKNOWN;
}

struct Parsed {
    std::string text;
    std::vector<text_span_t> spans;
    bool truncated = false;
};

Parsed parse(const std::string& in, size_t max_spans = 16, text_font_resolver_t r = resolver, void* ctx = nullptr) {
    std::vector<char> out(in.size() + 1, '\x7f');
    std::vector<text_span_t> spans(max_spans == 0 ? 1 : max_spans);
    const text_parse_result_t res =
        text_parse(in.data(), in.size(), out.data(), spans.data(), max_spans, r, ctx);
    TEST_ASSERT_TRUE(res.text_len <= in.size());  // never longer than the input
    TEST_ASSERT_EQUAL_CHAR('\0', out[res.text_len]);
    Parsed p;
    p.text.assign(out.data(), res.text_len);
    p.spans.assign(spans.begin(), spans.begin() + res.span_count);
    p.truncated = res.truncated;
    // spans cover the text in order, without gaps or empty spans
    size_t at = 0;
    for (const auto& s : p.spans) {
        TEST_ASSERT_EQUAL_UINT(at, s.offset);
        TEST_ASSERT_TRUE(s.len > 0);
        at += s.len;
    }
    TEST_ASSERT_EQUAL_UINT(p.text.size(), at);
    return p;
}

std::string span_text(const Parsed& p, size_t i) {
    return p.text.substr(p.spans[i].offset, p.spans[i].len);
}

// Text is kept as is, in one span with no attributes.
void assert_literal(const std::string& in) {
    const Parsed p = parse(in);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(in.c_str(), p.text.c_str(), in.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(in.empty() ? 0 : 1, p.spans.size(), in.c_str());
    if (!in.empty()) {
        TEST_ASSERT_EQUAL_INT(TEXT_FONT_BASE, p.spans[0].font);
        TEST_ASSERT_EQUAL_UINT8(TEXT_SIZE_BASE, p.spans[0].size_kind);
        TEST_ASSERT_EQUAL_UINT8(0, p.spans[0].has_color);
        TEST_ASSERT_EQUAL_UINT8(TEXT_ALIGN_KEEP, p.spans[0].align);
        TEST_ASSERT_EQUAL_UINT8(TEXT_VALIGN_KEEP, p.spans[0].valign);
    }
}

} // namespace

static void test_plain_text() {
    const Parsed p = parse("21.5");
    TEST_ASSERT_EQUAL_STRING("21.5", p.text.c_str());
    TEST_ASSERT_EQUAL_UINT(1, p.spans.size());
    assert_literal("hello world");
    assert_literal("caf\xc3\xa9");  // UTF-8 passes through
    const Parsed e = parse("");
    TEST_ASSERT_EQUAL_UINT(0, e.spans.size());
    TEST_ASSERT_EQUAL_STRING("", e.text.c_str());
}

static void test_backslash() {
    const Parsed p = parse("a\\\\b");
    TEST_ASSERT_EQUAL_STRING("a\\b", p.text.c_str());
    TEST_ASSERT_EQUAL_UINT(1, p.spans.size());
    TEST_ASSERT_EQUAL_STRING("\\", parse("\\").text.c_str());          // trailing backslash
    TEST_ASSERT_EQUAL_STRING("ab\\", parse("ab\\").text.c_str());
    TEST_ASSERT_EQUAL_STRING("\\\\", parse("\\\\\\\\").text.c_str());  // two escaped
    TEST_ASSERT_EQUAL_STRING("\\x", parse("\\x").text.c_str());        // unknown escape
    TEST_ASSERT_EQUAL_STRING("\\\\", parse("\\\\\\").text.c_str());     // escaped + trailing
}

static void test_legacy_steps() {
    for (int d = 1; d <= 9; d++) {
        const std::string in = std::string("\\s") + char('0' + d) + "x";
        const Parsed p = parse(in);
        TEST_ASSERT_EQUAL_STRING("x", p.text.c_str());
        TEST_ASSERT_EQUAL_UINT(1, p.spans.size());
        TEST_ASSERT_EQUAL_UINT8(TEXT_SIZE_STEP, p.spans[0].size_kind);
        TEST_ASSERT_EQUAL_UINT8(d, p.spans[0].size);
    }
    // \s0 = back to the item's size
    const Parsed p = parse("a\\s3b\\s0c");
    TEST_ASSERT_EQUAL_STRING("abc", p.text.c_str());
    TEST_ASSERT_EQUAL_UINT(3, p.spans.size());
    TEST_ASSERT_EQUAL_UINT8(TEXT_SIZE_BASE, p.spans[0].size_kind);
    TEST_ASSERT_EQUAL_UINT8(TEXT_SIZE_STEP, p.spans[1].size_kind);
    TEST_ASSERT_EQUAL_UINT8(3, p.spans[1].size);
    TEST_ASSERT_EQUAL_UINT8(TEXT_SIZE_BASE, p.spans[2].size_kind);
    TEST_ASSERT_EQUAL_UINT8(0, p.spans[2].size);
    // \s not followed by a digit or { is literal
    assert_literal("\\sx");
    assert_literal("\\s");
}

static void test_px_size() {
    const Parsed p = parse("21\\s{20}\xc2\xb0" "C");
    TEST_ASSERT_EQUAL_STRING("21\xc2\xb0" "C", p.text.c_str());
    TEST_ASSERT_EQUAL_UINT(2, p.spans.size());
    TEST_ASSERT_EQUAL_STRING("21", span_text(p, 0).c_str());
    TEST_ASSERT_EQUAL_UINT8(TEXT_SIZE_BASE, p.spans[0].size_kind);
    TEST_ASSERT_EQUAL_UINT8(TEXT_SIZE_PX, p.spans[1].size_kind);
    TEST_ASSERT_EQUAL_UINT8(20, p.spans[1].size);
    // bounds: 6 and 72 are px, 5 and 73 literal
    TEST_ASSERT_EQUAL_UINT8(6, parse("\\s{6}x").spans[0].size);
    TEST_ASSERT_EQUAL_UINT8(72, parse("\\s{72}x").spans[0].size);
    TEST_ASSERT_EQUAL_UINT8(40, parse("\\s{040}x").spans[0].size);  // 3 digits
    assert_literal("\\s{5}x");
    assert_literal("\\s{73}x");
    assert_literal("\\s{0}x");
    assert_literal("\\s{}x");
    assert_literal("\\s{0040}x");  // 4 digits
    assert_literal("\\s{4a}x");
    assert_literal("\\s{40");      // not closed
    assert_literal("\\s{");
}

static void test_color() {
    const Parsed p = parse("a\\cff0000b\\c000000c");
    TEST_ASSERT_EQUAL_STRING("abc", p.text.c_str());
    TEST_ASSERT_EQUAL_UINT(3, p.spans.size());
    TEST_ASSERT_EQUAL_UINT8(0, p.spans[0].has_color);
    TEST_ASSERT_EQUAL_UINT8(1, p.spans[1].has_color);
    TEST_ASSERT_EQUAL_HEX32(0xff0000, p.spans[1].color);
    TEST_ASSERT_EQUAL_UINT8(1, p.spans[2].has_color);  // black is a colour now
    TEST_ASSERT_EQUAL_HEX32(0x000000, p.spans[2].color);
    TEST_ASSERT_EQUAL_HEX32(0xABCDEF, parse("\\cABCdefx").spans[0].color);
    assert_literal("\\cff00");       // too short
    assert_literal("\\cff00zz");     // not hex
    assert_literal("\\c");
}

static void test_alignment() {
    const Parsed p = parse("\\ara\\vtb\\acc\\vbd\\ale\\vmf");
    TEST_ASSERT_EQUAL_STRING("abcdef", p.text.c_str());
    TEST_ASSERT_EQUAL_UINT(6, p.spans.size());
    TEST_ASSERT_EQUAL_UINT8(TEXT_ALIGN_RIGHT, p.spans[0].align);
    TEST_ASSERT_EQUAL_UINT8(TEXT_VALIGN_KEEP, p.spans[0].valign);
    TEST_ASSERT_EQUAL_UINT8(TEXT_ALIGN_RIGHT, p.spans[1].align);  // holds
    TEST_ASSERT_EQUAL_UINT8(TEXT_VALIGN_TOP, p.spans[1].valign);
    TEST_ASSERT_EQUAL_UINT8(TEXT_ALIGN_CENTER, p.spans[2].align);
    TEST_ASSERT_EQUAL_UINT8(TEXT_VALIGN_BOTTOM, p.spans[3].valign);
    TEST_ASSERT_EQUAL_UINT8(TEXT_ALIGN_LEFT, p.spans[4].align);
    TEST_ASSERT_EQUAL_UINT8(TEXT_VALIGN_MIDDLE, p.spans[5].valign);
    assert_literal("\\ax");
    assert_literal("\\a");
    assert_literal("\\vx");
    assert_literal("\\v");
}

static void test_unicode_4() {
    TEST_ASSERT_EQUAL_STRING("A", parse("\\u0041").text.c_str());
    TEST_ASSERT_EQUAL_STRING("\xc2\xb0", parse("\\u00b0").text.c_str());      // degree sign
    TEST_ASSERT_EQUAL_STRING("\xe2\x9c\x93", parse("\\u2713").text.c_str());  // check mark
    TEST_ASSERT_EQUAL_STRING("\xef\xbf\xbf", parse("\\uFFFF").text.c_str());
    TEST_ASSERT_EQUAL_STRING("\xc2\xb0" "C", parse("\\u00B0C").text.c_str());  // 4 digits only
    assert_literal("\\u00b");     // too short
    assert_literal("\\u00g0");    // not hex
    assert_literal("\\uD800");    // surrogate
    assert_literal("\\udfff");
    assert_literal("\\u0000");    // NUL
    assert_literal("\\u");
}

static void test_unicode_braced() {
    const Parsed p = parse("\\u{F0D43}");
    TEST_ASSERT_EQUAL_STRING("\xf3\xb0\xb5\x83", p.text.c_str());  // U+F0D43, 4 bytes
    TEST_ASSERT_EQUAL_UINT(4, p.text.size());
    TEST_ASSERT_EQUAL_STRING("A", parse("\\u{41}").text.c_str());
    TEST_ASSERT_EQUAL_STRING("\t", parse("\\u{9}").text.c_str());
    TEST_ASSERT_EQUAL_STRING("\xf4\x8f\xbf\xbf", parse("\\u{10FFFF}").text.c_str());
    TEST_ASSERT_EQUAL_STRING("\xf0\x9f\x98\x80", parse("\\u{01F600}").text.c_str());
    assert_literal("\\u{110000}");  // too large
    assert_literal("\\u{D83D}");    // surrogate
    assert_literal("\\u{0}");
    assert_literal("\\u{}");
    assert_literal("\\u{1000000}"); // 7 digits
    assert_literal("\\u{F0D43");    // not closed
    assert_literal("\\u{F0X43}");
}

static void test_font() {
    int calls = 0;
    const Parsed p = parse("\\f{mono}12:30 \\f{7}x\\f{}y", 16, resolver, &calls);
    TEST_ASSERT_EQUAL_INT(2, calls);
    TEST_ASSERT_EQUAL_STRING("12:30 xy", p.text.c_str());
    TEST_ASSERT_EQUAL_UINT(3, p.spans.size());
    TEST_ASSERT_EQUAL_INT(5, p.spans[0].font);
    TEST_ASSERT_EQUAL_STRING("12:30 ", span_text(p, 0).c_str());
    TEST_ASSERT_EQUAL_INT(4, p.spans[1].font);
    TEST_ASSERT_EQUAL_INT(TEXT_FONT_BASE, p.spans[2].font);
    // unknown name / id: TEXT_FONT_UNKNOWN (the escape itself is consumed)
    const Parsed u = parse("\\f{nope}a\\f{99}b");
    TEST_ASSERT_EQUAL_STRING("ab", u.text.c_str());
    TEST_ASSERT_EQUAL_UINT(1, u.spans.size());
    TEST_ASSERT_EQUAL_INT(TEXT_FONT_UNKNOWN, u.spans[0].font);
    // no resolver: every font is unknown
    TEST_ASSERT_EQUAL_INT(TEXT_FONT_UNKNOWN, parse("\\f{mono}a", 16, nullptr).spans[0].font);
    // \f{} is not a lookup
    calls = 0;
    TEST_ASSERT_EQUAL_INT(TEXT_FONT_BASE, parse("\\f{}a", 16, resolver, &calls).spans[0].font);
    TEST_ASSERT_EQUAL_INT(0, calls);
    // the id is the number: is_id only for all digits
    TEST_ASSERT_EQUAL_INT(4, parse("\\f{007}a").spans[0].font);
    TEST_ASSERT_EQUAL_INT(TEXT_FONT_UNKNOWN, parse("\\f{7a}a").spans[0].font);
    TEST_ASSERT_EQUAL_INT(TEXT_FONT_UNKNOWN, parse("\\f{99999999999}a").spans[0].font);  // saturates
    // malformed: literal
    assert_literal("\\fx");
    assert_literal("\\f");
    assert_literal("\\f{mono");
    assert_literal("\\f{mo no}x");
    assert_literal("\\f{mo-no}x");
}

static void test_image_dropped() {
    TEST_ASSERT_EQUAL_STRING("ab", parse("a\\i{sun}b").text.c_str());
    TEST_ASSERT_EQUAL_UINT(1, parse("a\\i{sun}b").spans.size());
    TEST_ASSERT_EQUAL_STRING("", parse("\\i{x}").text.c_str());
    TEST_ASSERT_EQUAL_UINT(0, parse("\\i{x}").spans.size());
    const std::string n63(63, 'n');
    TEST_ASSERT_EQUAL_STRING("", parse("\\i{" + n63 + "}").text.c_str());
    assert_literal("\\i{" + n63 + "n}");  // 64 bytes
    assert_literal("\\i{}");
    assert_literal("\\i{abc");
    assert_literal("\\ix");
    assert_literal("\\i");
}

static void test_span_merging() {
    // an escape that changes nothing keeps one span
    TEST_ASSERT_EQUAL_UINT(1, parse("a\\s0b\\f{}c\\\\d").spans.size());
    TEST_ASSERT_EQUAL_UINT(1, parse("\\cff0000a\\cff0000b").spans.size());
    // escapes without text after them make no span
    const Parsed p = parse("ab\\s3\\cff0000\\ar");
    TEST_ASSERT_EQUAL_STRING("ab", p.text.c_str());
    TEST_ASSERT_EQUAL_UINT(1, p.spans.size());
    TEST_ASSERT_EQUAL_UINT8(TEXT_SIZE_BASE, p.spans[0].size_kind);
    // a later escape overrides an earlier one before any text
    const Parsed q = parse("\\s2\\s4x");
    TEST_ASSERT_EQUAL_UINT(1, q.spans.size());
    TEST_ASSERT_EQUAL_UINT8(4, q.spans[0].size);
    // attributes hold across escapes that change something else
    const Parsed r = parse("\\cff0000a\\s3b");
    TEST_ASSERT_EQUAL_UINT(2, r.spans.size());
    TEST_ASSERT_EQUAL_UINT8(1, r.spans[1].has_color);
    TEST_ASSERT_EQUAL_HEX32(0xff0000, r.spans[1].color);
    TEST_ASSERT_EQUAL_UINT8(3, r.spans[1].size);
    // a \u escape joins the span around it
    TEST_ASSERT_EQUAL_UINT(1, parse("a\\u00b0b").spans.size());
}

static void test_truncation() {
    const Parsed p = parse("a\\s2b\\s3c\\s4d", 2);
    TEST_ASSERT_TRUE(p.truncated);
    TEST_ASSERT_EQUAL_STRING("abcd", p.text.c_str());
    TEST_ASSERT_EQUAL_UINT(2, p.spans.size());
    TEST_ASSERT_EQUAL_STRING("bcd", span_text(p, 1).c_str());  // the last runs to the end
    TEST_ASSERT_EQUAL_UINT8(2, p.spans[1].size);                // with its attributes
    TEST_ASSERT_FALSE(parse("a\\s2b", 2).truncated);
    // no span room at all
    std::vector<char> out(8);
    text_span_t one;
    text_parse_result_t r = text_parse("ab", 2, out.data(), &one, 0, nullptr, nullptr);
    TEST_ASSERT_TRUE(r.truncated);
    TEST_ASSERT_EQUAL_UINT(0, r.span_count);
    TEST_ASSERT_EQUAL_STRING("ab", out.data());
    // counting only: spans needed
    r = text_parse("a\\s2b\\s3c", 9, out.data(), nullptr, 0, nullptr, nullptr);
    TEST_ASSERT_EQUAL_UINT(3, r.span_count);
    TEST_ASSERT_FALSE(r.truncated);
    TEST_ASSERT_EQUAL_STRING("abc", out.data());
}

static void test_not_nul_terminated() {
    // len bounds the input: bytes after it are not read
    const char in[] = {'a', '\\', 's', '{', '4', '0', '}', 'b', '\\', 's', '{', '4'};
    std::vector<char> out(sizeof(in) + 1);
    text_span_t spans[4];
    const text_parse_result_t r = text_parse(in, 8, out.data(), spans, 4, nullptr, nullptr);
    TEST_ASSERT_EQUAL_STRING("ab", out.data());
    TEST_ASSERT_EQUAL_UINT(2, r.span_count);
    const text_parse_result_t t = text_parse(in, sizeof(in), out.data(), spans, 4, nullptr, nullptr);
    TEST_ASSERT_EQUAL_STRING("ab\\s{4", out.data());
    TEST_ASSERT_EQUAL_UINT(6, t.text_len);
}

static void test_never_longer() {
    // every escape and malformed form, alone and at the end of the input
    const char* cases[] = {"\\",       "\\\\",     "\\s",      "\\s1",     "\\s{",     "\\s{7}",
                           "\\s{999}", "\\c",      "\\c12345", "\\c123456", "\\a",      "\\al",
                           "\\v",      "\\vb",     "\\u",      "\\u12",    "\\u1234",  "\\u{",
                           "\\u{1}",   "\\u{FFFF}", "\\u{10000}", "\\f",    "\\f{",     "\\f{x}",
                           "\\i",      "\\i{",     "\\i{a}",   "\\q"};
    for (const char* c : cases) {
        parse(c);
        parse(std::string("xy") + c);
        parse(std::string(c) + c);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_plain_text);
    RUN_TEST(test_backslash);
    RUN_TEST(test_legacy_steps);
    RUN_TEST(test_px_size);
    RUN_TEST(test_color);
    RUN_TEST(test_alignment);
    RUN_TEST(test_unicode_4);
    RUN_TEST(test_unicode_braced);
    RUN_TEST(test_font);
    RUN_TEST(test_image_dropped);
    RUN_TEST(test_span_merging);
    RUN_TEST(test_truncation);
    RUN_TEST(test_not_nul_terminated);
    RUN_TEST(test_never_longer);
    return UNITY_END();
}
