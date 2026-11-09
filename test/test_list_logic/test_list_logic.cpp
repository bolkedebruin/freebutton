//
// Native unit tests for the main-display list's pure logic
// (src/components/ListLogic.h): options string, selection, rows, colours.
//
#include <unity.h>

#include "../../src/components/ListLogic.h"

using namespace freebutton::list;

void setUp() {}
void tearDown() {}

static void test_append_items() {
    std::string options;
    int count = 0;
    TEST_ASSERT_TRUE(appendItem(options, count, "Bed Time"));
    TEST_ASSERT_TRUE(appendItem(options, count, "Evening"));
    TEST_ASSERT_EQUAL_STRING("Bed Time\nEvening", options.c_str());
    TEST_ASSERT_EQUAL(2, count);
}

static void test_newline_in_item_becomes_space() {
    std::string options;
    int count = 0;
    appendItem(options, count, "a\nb\rc");
    TEST_ASSERT_EQUAL_STRING("a b c", options.c_str());
    TEST_ASSERT_EQUAL(1, count);
}

static void test_empty_item_kept() {
    std::string options;
    int count = 0;
    appendItem(options, count, "");
    appendItem(options, count, "x");
    TEST_ASSERT_EQUAL_STRING("\nx", options.c_str());
    TEST_ASSERT_EQUAL(2, count);
}

static void test_full_list() {
    std::string options;
    int count = 0;
    for (int i = 0; i < kMaxItems; ++i) {
        TEST_ASSERT_TRUE(appendItem(options, count, "i"));
    }
    TEST_ASSERT_FALSE(appendItem(options, count, "more"));
    TEST_ASSERT_EQUAL(kMaxItems, count);
}

static void test_long_item_cut_on_utf8_boundary() {
    // 47 ASCII bytes + "é" (2 bytes) = 49: the cut at 48 would split "é".
    std::string item(47, 'a');
    item += "\xC3\xA9";
    TEST_ASSERT_EQUAL_size_t(47, itemLength(item));
    std::string ascii(60, 'b');
    TEST_ASSERT_EQUAL_size_t(kMaxItemBytes, itemLength(ascii));
}

static void test_clamp_selected() {
    TEST_ASSERT_EQUAL(-1, clampSelected(0, 0));
    TEST_ASSERT_EQUAL(0, clampSelected(-3, 2));
    TEST_ASSERT_EQUAL(1, clampSelected(1, 2));
    TEST_ASSERT_EQUAL(1, clampSelected(9, 2));
}

static void test_clamp_rows() {
    TEST_ASSERT_EQUAL(1, clampRows(0));
    TEST_ASSERT_EQUAL(3, clampRows(3));
    TEST_ASSERT_EQUAL(7, clampRows(12));
}

static void test_parse_color() {
    TEST_ASSERT_EQUAL_HEX32(0xFFD45F, parseColor("ffd45f", 0));
    TEST_ASSERT_EQUAL_HEX32(0x808080, parseColor("#808080", 0));
    TEST_ASSERT_EQUAL_HEX32(0xABCDEF, parseColor("ABCDEF", 0));
    TEST_ASSERT_EQUAL_HEX32(0x123456, parseColor("12345", 0x123456));
    TEST_ASSERT_EQUAL_HEX32(0x123456, parseColor("zz0000", 0x123456));
    TEST_ASSERT_EQUAL_HEX32(0x123456, parseColor("", 0x123456));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_append_items);
    RUN_TEST(test_newline_in_item_becomes_space);
    RUN_TEST(test_empty_item_kept);
    RUN_TEST(test_full_list);
    RUN_TEST(test_long_item_cut_on_utf8_boundary);
    RUN_TEST(test_clamp_selected);
    RUN_TEST(test_clamp_rows);
    RUN_TEST(test_parse_color);
    return UNITY_END();
}
