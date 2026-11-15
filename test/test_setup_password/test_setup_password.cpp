//
// Native unit tests for the WiFi setup access point password
// (src/utils/SetupPassword.h): form, alphabet, and that every character of
// the alphabet is equally likely (5 bits of a byte, no modulo).
//
#include <unity.h>

#include <cstring>

#include "../../src/utils/SetupPassword.h"

using freebutton::generateSetupPassword;
using freebutton::isSetupPasswordChar;
using freebutton::isValidSetupPassword;
using freebutton::kSetupPasswordAlphabet;
using freebutton::kSetupPasswordLen;

void setUp() {}
void tearDown() {}

void test_alphabet_has_32_distinct_characters_without_lookalikes() {
    TEST_ASSERT_EQUAL_size_t(32, strlen(kSetupPasswordAlphabet));
    for (size_t i = 0; i < 32; ++i) {
        for (size_t j = i + 1; j < 32; ++j) {
            TEST_ASSERT_TRUE(kSetupPasswordAlphabet[i] != kSetupPasswordAlphabet[j]);
        }
    }
    for (const char* c = "0O1Il"; *c; ++c) {
        TEST_ASSERT_NULL(strchr(kSetupPasswordAlphabet, *c));
    }
    // Nothing that needs escaping in a WiFi QR code, and not the separator.
    for (const char* c = "\\;,:\"-"; *c; ++c) {
        TEST_ASSERT_NULL(strchr(kSetupPasswordAlphabet, *c));
    }
}

void test_length_is_within_wpa2_and_holds_60_bits() {
    TEST_ASSERT_EQUAL_size_t(13, kSetupPasswordLen);
    TEST_ASSERT_TRUE(kSetupPasswordLen >= 8 && kSetupPasswordLen <= 63);
    // 12 random characters of 5 bits each.
    TEST_ASSERT_EQUAL_size_t(60, (kSetupPasswordLen - 1) * 5);
}

void test_generated_form() {
    uint32_t state = 12345;
    auto rng = [&state]() {
        state = state * 1103515245u + 12345u;
        return static_cast<uint8_t>(state >> 16);
    };
    char pw[kSetupPasswordLen + 1];
    for (int round = 0; round < 200; ++round) {
        memset(pw, 'x', sizeof(pw));
        generateSetupPassword(rng, pw);
        TEST_ASSERT_EQUAL_size_t(13, strlen(pw));
        TEST_ASSERT_EQUAL_CHAR('-', pw[6]);
        for (size_t i = 0; i < kSetupPasswordLen; ++i) {
            if (i != 6) TEST_ASSERT_TRUE(isSetupPasswordChar(pw[i]));
        }
        TEST_ASSERT_TRUE(isValidSetupPassword(pw));
    }
}

void test_one_random_byte_per_character() {
    int calls = 0;
    auto rng = [&calls]() {
        ++calls;
        return static_cast<uint8_t>(0);
    };
    char pw[kSetupPasswordLen + 1];
    generateSetupPassword(rng, pw);
    TEST_ASSERT_EQUAL(12, calls);
    TEST_ASSERT_EQUAL_STRING("AAAAAA-AAAAAA", pw);
}

// Each of the 256 byte values in turn: every character of the alphabet must
// come out exactly 8 times (256 / 32). A modulo of a non-power-of-two
// alphabet would fail this.
void test_no_bias_over_all_byte_values() {
    int count[32] = {};
    char pw[kSetupPasswordLen + 1];
    for (int value = 0; value < 256; ++value) {
        auto rng = [value]() { return static_cast<uint8_t>(value); };
        generateSetupPassword(rng, pw);
        const char* at = strchr(kSetupPasswordAlphabet, pw[0]);
        TEST_ASSERT_NOT_NULL(at);
        ++count[at - kSetupPasswordAlphabet];
        // The same byte gives the same character at every position.
        for (size_t i = 0; i < kSetupPasswordLen; ++i) {
            if (i != 6) TEST_ASSERT_EQUAL_CHAR(pw[0], pw[i]);
        }
    }
    for (int i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL(8, count[i]);
    }
}

void test_validation_rejects_other_forms() {
    TEST_ASSERT_TRUE(isValidSetupPassword("ABCDEF-GHJKMN"));
    TEST_ASSERT_TRUE(isValidSetupPassword("234567-89ZYXW"));
    TEST_ASSERT_FALSE(isValidSetupPassword(nullptr));
    TEST_ASSERT_FALSE(isValidSetupPassword(""));
    TEST_ASSERT_FALSE(isValidSetupPassword("fba00001"));        // the old MAC-derived form
    TEST_ASSERT_FALSE(isValidSetupPassword("ABCDEF-GHJKM"));    // too short
    TEST_ASSERT_FALSE(isValidSetupPassword("ABCDEF-GHJKMNP"));  // too long
    TEST_ASSERT_FALSE(isValidSetupPassword("ABCDEFGHJKMNP"));   // no separator
    TEST_ASSERT_FALSE(isValidSetupPassword("ABCDE-FGHJKMN"));   // separator in the wrong place
    TEST_ASSERT_FALSE(isValidSetupPassword("abcdef-ghjkmn"));   // lower case
    TEST_ASSERT_FALSE(isValidSetupPassword("ABCDE0-GHJKMN"));   // '0'
    TEST_ASSERT_FALSE(isValidSetupPassword("ABCDEO-GHJKMN"));   // 'O'
    TEST_ASSERT_FALSE(isValidSetupPassword("ABCDE1-GHJKMN"));   // '1'
    TEST_ASSERT_FALSE(isValidSetupPassword("ABCDEI-GHJKMN"));   // 'I'
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_alphabet_has_32_distinct_characters_without_lookalikes);
    RUN_TEST(test_length_is_within_wpa2_and_holds_60_bits);
    RUN_TEST(test_generated_form);
    RUN_TEST(test_one_random_byte_per_character);
    RUN_TEST(test_no_bias_over_all_byte_values);
    RUN_TEST(test_validation_rejects_other_forms);
    return UNITY_END();
}
