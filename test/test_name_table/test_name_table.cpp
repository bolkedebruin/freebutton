//
// Native unit tests for the config name table (src/components/NameTable.h):
// name rules, the two namespaces, duplicates, lookups both ways.
//
#include <unity.h>

#include "../../src/components/NameTable.h"

using freebutton::NameTable;
using Kind = NameTable::Kind;
using Result = NameTable::AddResult;

void setUp() {}
void tearDown() {}

static void test_valid_names() {
    TEST_ASSERT_TRUE(NameTable::valid("volume"));
    TEST_ASSERT_TRUE(NameTable::valid("_x"));
    TEST_ASSERT_TRUE(NameTable::valid("sceneUp2"));
    TEST_ASSERT_TRUE(NameTable::valid("abcdefghijklmnopqrstuvwx"));  // 24
    TEST_ASSERT_FALSE(NameTable::valid(""));
    TEST_ASSERT_FALSE(NameTable::valid("2up"));
    TEST_ASSERT_FALSE(NameTable::valid("scene up"));
    TEST_ASSERT_FALSE(NameTable::valid("scène"));
    TEST_ASSERT_FALSE(NameTable::valid("a-b"));
    TEST_ASSERT_FALSE(NameTable::valid("abcdefghijklmnopqrstuvwxy"));  // 25
}

static void test_add_and_find() {
    NameTable t;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Added), static_cast<int>(t.add(Kind::DisplayItem, 8, "volume")));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Added), static_cast<int>(t.add(Kind::Key, 3, "sceneUp")));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Added), static_cast<int>(t.add(Kind::DisplayItem, 70000, "big")));

    uint32_t id = 0;
    TEST_ASSERT_TRUE(t.find(Kind::DisplayItem, "volume", id));
    TEST_ASSERT_EQUAL_UINT32(8, id);
    TEST_ASSERT_TRUE(t.find(Kind::DisplayItem, "big", id));
    TEST_ASSERT_EQUAL_UINT32(70000, id);
    TEST_ASSERT_TRUE(t.find(Kind::Key, "sceneUp", id));
    TEST_ASSERT_EQUAL_UINT32(3, id);

    TEST_ASSERT_FALSE(t.find(Kind::Key, "volume", id));         // other namespace
    TEST_ASSERT_FALSE(t.find(Kind::DisplayItem, "volum", id));  // prefix
    TEST_ASSERT_FALSE(t.find(Kind::DisplayItem, "volumes", id));
    TEST_ASSERT_FALSE(t.find(Kind::DisplayItem, "", id));
}

static void test_duplicates_and_invalid() {
    NameTable t;
    t.add(Kind::DisplayItem, 1, "clock");
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Duplicate), static_cast<int>(t.add(Kind::DisplayItem, 2, "clock")));
    uint32_t id = 0;
    TEST_ASSERT_TRUE(t.find(Kind::DisplayItem, "clock", id));
    TEST_ASSERT_EQUAL_UINT32(1, id);  // the first one stays
    // The same name in the other namespace is fine.
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Added), static_cast<int>(t.add(Kind::Key, 2, "clock")));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Invalid), static_cast<int>(t.add(Kind::Key, 4, "no way")));
    TEST_ASSERT_FALSE(t.find(Kind::Key, "no way", id));
}

static void test_name_of() {
    NameTable t;
    t.add(Kind::DisplayItem, 5, "filter");
    t.add(Kind::Key, 5, "windowUp");
    TEST_ASSERT_TRUE(t.nameOf(Kind::DisplayItem, 5) == "filter");
    TEST_ASSERT_TRUE(t.nameOf(Kind::Key, 5) == "windowUp");
    TEST_ASSERT_TRUE(t.nameOf(Kind::Key, 6).empty());
}

static void test_size_and_clear() {
    NameTable t;
    t.add(Kind::DisplayItem, 8, "volume");  // 6 + 6
    t.add(Kind::Key, 3, "up");              // 6 + 2
    t.compact();
    TEST_ASSERT_EQUAL_size_t(20, t.bytes());
    t.clear();
    uint32_t id = 0;
    TEST_ASSERT_FALSE(t.find(Kind::DisplayItem, "volume", id));
    TEST_ASSERT_EQUAL_size_t(0, t.bytes());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_names);
    RUN_TEST(test_add_and_find);
    RUN_TEST(test_duplicates_and_invalid);
    RUN_TEST(test_name_of);
    RUN_TEST(test_size_and_clear);
    return UNITY_END();
}
