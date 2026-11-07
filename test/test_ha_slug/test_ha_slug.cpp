//
// Native unit tests for haSlugify (HA-style slug used for default_entity_id).
// The .cpp is included directly (same pattern as test_deviceinfo).
//
#include <unity.h>
#include <string>

#include "../../src/mqtt/HaSlug.cpp"

void setUp() {}
void tearDown() {}

static void check(const char* expected, const char* input) {
    TEST_ASSERT_EQUAL_STRING(expected, haSlugify(input).c_str());
}

void test_plain_name() { check("buttonplus_garage", "ButtonPlus Garage"); }
void test_dash_and_punctuation() { check("keuken_links", "Keuken \xE2\x80\x93 Links!"); }   // en dash
void test_latin1_accents() { check("cafe_een", "Caf\xC3\xA9 \xC3\x89\xC3\xA9n"); }        // Café Één
void test_trim_and_collapse() { check("x", "  __x__ "); }
void test_sharp_s() { check("strasse", "Stra\xC3\x9F" "e"); }                              // Straße
void test_device_id_unchanged() { check("btn_a00003", "btn_a00003"); }
void test_only_punctuation() { check("", "!!!"); }
void test_empty() { check("", ""); }
void test_more_latin1() {
    check("aeoeu", "\xC3\xA6\xC3\xB8\xC3\xAB\xC3\xBC");   // æøëü
    check("ijssel", "\xC4\xB3ssel");                     // ĳssel
    check("a_b", "a\xC3\x97" "b");                       // a×b (× is a separator)
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_plain_name);
    RUN_TEST(test_dash_and_punctuation);
    RUN_TEST(test_latin1_accents);
    RUN_TEST(test_trim_and_collapse);
    RUN_TEST(test_sharp_s);
    RUN_TEST(test_device_id_unchanged);
    RUN_TEST(test_only_punctuation);
    RUN_TEST(test_empty);
    RUN_TEST(test_more_latin1);
    return UNITY_END();
}
