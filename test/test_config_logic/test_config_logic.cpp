//
// Native unit tests for the config loader's small rules
// (src/components/ConfigLogic.h).
//
#include <unity.h>
#include <ArduinoJson.h>

#include "../../src/components/ConfigLogic.h"

using namespace freebutton;

void setUp() {}
void tearDown() {}

static bool discovery(const char* brokerJson) {
    ArduinoJson::JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, brokerJson) == ArduinoJson::DeserializationError::Ok);
    return configlogic::hassDiscovery(doc["hassdiscovery"]);
}

// Absent = on; otherwise the value as a boolean.
void test_hassdiscovery_absent_is_on() {
    TEST_ASSERT_TRUE(discovery("{}"));
    TEST_ASSERT_TRUE(discovery("{\"hassdiscovery\": null}"));
    TEST_ASSERT_TRUE(discovery("{\"url\": \"mqtt://x\"}"));
}

void test_hassdiscovery_booleans() {
    TEST_ASSERT_TRUE(discovery("{\"hassdiscovery\": true}"));
    TEST_ASSERT_FALSE(discovery("{\"hassdiscovery\": false}"));
}

// The vendor's configs also wrote numbers. `value | true` read 0 as true.
void test_hassdiscovery_values_that_are_no_boolean() {
    TEST_ASSERT_FALSE(discovery("{\"hassdiscovery\": 0}"));
    TEST_ASSERT_TRUE(discovery("{\"hassdiscovery\": 1}"));
    TEST_ASSERT_FALSE(discovery("{\"hassdiscovery\": 0.0}"));
    // What as<bool>() makes of the rest (as before this work): a string
    // and an object are there, so they are on.
    TEST_ASSERT_TRUE(discovery("{\"hassdiscovery\": \"false\"}"));
    TEST_ASSERT_TRUE(discovery("{\"hassdiscovery\": \"true\"}"));
    TEST_ASSERT_TRUE(discovery("{\"hassdiscovery\": {}}"));
}

// The removed key is not read: discovery stays on whatever it says.
void test_autodiscovery_is_not_read() {
    TEST_ASSERT_TRUE(discovery("{\"autoDiscovery\": false}"));
    TEST_ASSERT_FALSE(discovery("{\"autoDiscovery\": true, \"hassdiscovery\": false}"));
}

static bool hasPosition(const char* buttonJson) {
    ArduinoJson::JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buttonJson) == ArduinoJson::DeserializationError::Ok);
    return configlogic::buttonHasPosition(doc.as<ArduinoJson::JsonVariantConst>());
}

void test_a_button_needs_a_position() {
    TEST_ASSERT_TRUE(hasPosition("{\"position\": 3, \"name\": \"a\"}"));
    TEST_ASSERT_FALSE(hasPosition("{\"name\": \"a\", \"label\": \"x\"}"));
    TEST_ASSERT_FALSE(hasPosition("{\"position\": null}"));
    // the vendor's fallback is gone: an "id" is no position
    TEST_ASSERT_FALSE(hasPosition("{\"id\": 3, \"name\": \"a\"}"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hassdiscovery_absent_is_on);
    RUN_TEST(test_hassdiscovery_booleans);
    RUN_TEST(test_hassdiscovery_values_that_are_no_boolean);
    RUN_TEST(test_autodiscovery_is_not_read);
    RUN_TEST(test_a_button_needs_a_position);
    return UNITY_END();
}
