//
// Native unit tests for the icon / image config rules
// (src/components/IconLogic.h): icon names, sizes, colours, and the
// image item's "what to show" encoding; plus the image namespace of the
// config name table.
//
#include <unity.h>

#include <cstring>

#include "../../src/components/IconLogic.h"
#include "../../src/components/NameTable.h"
#include "../../src/utils/TextSlot.h"

namespace icon = freebutton::icon;
using freebutton::NameTable;

void setUp() {}
void tearDown() {}

static void test_valid_names() {
    TEST_ASSERT_TRUE(icon::validName("rain"));
    TEST_ASSERT_TRUE(icon::validName("weather-sunny_2"));
    TEST_ASSERT_TRUE(icon::validName("0"));
    TEST_ASSERT_TRUE(icon::validName("abcdefghijklmnopqrstuvwxyz01234"));    // 31
    TEST_ASSERT_FALSE(icon::validName("abcdefghijklmnopqrstuvwxyz012345"));  // 32
    TEST_ASSERT_FALSE(icon::validName(""));
    TEST_ASSERT_FALSE(icon::validName("a b"));
    TEST_ASSERT_FALSE(icon::validName("a.svg"));
    TEST_ASSERT_FALSE(icon::validName("../x"));
    TEST_ASSERT_FALSE(icon::validName("zon\xc3\xa9"));
}

static void test_clamp_size() {
    TEST_ASSERT_EQUAL(48, icon::clampSize(0, 48));
    TEST_ASSERT_EQUAL(32, icon::clampSize(-5, 32));
    TEST_ASSERT_EQUAL(24, icon::clampSize(24, 48));
    TEST_ASSERT_EQUAL(icon::kMaxSize, icon::clampSize(5000, 48));
}

static void test_parse_color() {
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, icon::parseColor("#ffd700"));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, icon::parseColor("00ff00"));
    TEST_ASSERT_EQUAL_HEX32(icon::kNoColor, icon::parseColor(""));
    TEST_ASSERT_EQUAL_HEX32(icon::kNoColor, icon::parseColor(nullptr));
    TEST_ASSERT_EQUAL_HEX32(icon::kNoColor, icon::parseColor("#fff"));
    TEST_ASSERT_EQUAL_HEX32(icon::kNoColor, icon::parseColor("#gg0000"));
}

static void test_encode_want() {
    char out[icon::kWantMax + 1];
    TEST_ASSERT_TRUE(icon::encodeWant(out, sizeof(out), icon::kWantIcon, "rain"));
    TEST_ASSERT_EQUAL_STRING("irain", out);
    TEST_ASSERT_TRUE(icon::encodeWant(out, sizeof(out), icon::kWantSrc, "radar/frame-1.bin"));
    TEST_ASSERT_EQUAL_STRING("sradar/frame-1.bin", out);
    TEST_ASSERT_TRUE(icon::encodeWant(out, sizeof(out), icon::kWantIcon, ""));
    TEST_ASSERT_EQUAL_STRING("", out);

    // Invalid icon names and over-long keys leave out untouched.
    std::strcpy(out, "keep");
    TEST_ASSERT_FALSE(icon::encodeWant(out, sizeof(out), icon::kWantIcon, "a b"));
    TEST_ASSERT_EQUAL_STRING("keep", out);
    char key[icon::kKeyMax + 2];
    std::memset(key, 'k', sizeof(key) - 1);
    key[sizeof(key) - 1] = '\0';  // 97 bytes
    TEST_ASSERT_FALSE(icon::encodeWant(out, sizeof(out), icon::kWantSrc, key));
    key[icon::kKeyMax] = '\0';  // 96 bytes: fits exactly
    TEST_ASSERT_TRUE(icon::encodeWant(out, sizeof(out), icon::kWantSrc, key));
    TEST_ASSERT_EQUAL(icon::kWantMax, std::strlen(out));

    // A buffer that cannot hold it.
    char small[4];
    TEST_ASSERT_FALSE(icon::encodeWant(small, sizeof(small), icon::kWantIcon, "rain"));
}

// The slots the items keep their icon / image state in hold the longest value.
static void test_slot_sizes() {
    TEST_ASSERT_TRUE(freebutton::TextSlot<40>::kCapacity >= icon::kNameMax);
    TEST_ASSERT_TRUE(freebutton::TextSlot<104>::kCapacity >= icon::kWantMax);
}

static void test_image_names() {
    NameTable t;
    TEST_ASSERT_EQUAL(static_cast<int>(NameTable::AddResult::Added),
                      static_cast<int>(t.add(NameTable::Kind::Image, 0, "weather")));
    // Separate namespace: a display item may have the same name.
    TEST_ASSERT_EQUAL(static_cast<int>(NameTable::AddResult::Added),
                      static_cast<int>(t.add(NameTable::Kind::DisplayItem, 4, "weather")));
    TEST_ASSERT_EQUAL(static_cast<int>(NameTable::AddResult::Duplicate),
                      static_cast<int>(t.add(NameTable::Kind::Image, 1, "weather")));
    uint32_t id = 99;
    TEST_ASSERT_TRUE(t.find(NameTable::Kind::Image, "weather", id));
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_FALSE(t.find(NameTable::Kind::List, "weather", id));
    TEST_ASSERT_TRUE(t.nameOf(NameTable::Kind::Image, 0) == "weather");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_names);
    RUN_TEST(test_clamp_size);
    RUN_TEST(test_parse_color);
    RUN_TEST(test_encode_want);
    RUN_TEST(test_slot_sizes);
    RUN_TEST(test_image_names);
    return UNITY_END();
}
