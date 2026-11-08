//
// Native unit tests for the JS MQTT layer's topic filter match.
//
#include <unity.h>
#include <cstring>

#include "../../src/scripting/js/mqtt_topic_match.h"

void setUp() {}
void tearDown() {}

static bool match(const char* filter, const char* topic) {
    return mqtt_topic_matches(filter, topic, std::strlen(topic)) != 0;
}

void test_exact() {
    TEST_ASSERT_TRUE(match("a/b", "a/b"));
    TEST_ASSERT_FALSE(match("a/b", "a/bc"));
    TEST_ASSERT_FALSE(match("a/b", "a"));
}

void test_length_bounded_topic() {
    // The topic is not NUL-terminated: only topic_len bytes count.
    const char buf[] = "a/b/c-garbage";
    TEST_ASSERT_TRUE(mqtt_topic_matches("a/b/c", buf, 5));
    TEST_ASSERT_FALSE(mqtt_topic_matches("a/b/c-", buf, 5));
    TEST_ASSERT_TRUE(mqtt_topic_matches("a/b/#", buf, 3));  // "a/b"
}

void test_hash_level_boundary() {
    TEST_ASSERT_TRUE(match("a/b/#", "a/b"));
    TEST_ASSERT_TRUE(match("a/b/#", "a/b/c"));
    TEST_ASSERT_TRUE(match("a/b/#", "a/b/c/d"));
    TEST_ASSERT_FALSE(match("a/b/#", "a/bc"));
    TEST_ASSERT_FALSE(match("a/b/#", "a"));
}

void test_hash_alone_and_leading_slash() {
    TEST_ASSERT_TRUE(match("#", "anything/at/all"));
    TEST_ASSERT_TRUE(match("#", ""));
    TEST_ASSERT_TRUE(match("/#", "/x"));
    TEST_ASSERT_FALSE(match("/#", "x"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_exact);
    RUN_TEST(test_length_bounded_topic);
    RUN_TEST(test_hash_level_boundary);
    RUN_TEST(test_hash_alone_and_leading_slash);
    return UNITY_END();
}
