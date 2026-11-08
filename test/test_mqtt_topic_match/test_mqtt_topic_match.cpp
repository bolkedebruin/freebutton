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

void test_plus_single_level() {
    TEST_ASSERT_TRUE(match("a/+/c", "a/x/c"));
    TEST_ASSERT_TRUE(match("a/+/c", "a//c"));  // empty level
    TEST_ASSERT_FALSE(match("a/+/c", "a/c"));
    TEST_ASSERT_FALSE(match("a/+/c", "a/x/y/c"));
    TEST_ASSERT_FALSE(match("a/+/c", "a/x/cd"));
    TEST_ASSERT_TRUE(match("a/+", "a/x"));
    TEST_ASSERT_FALSE(match("a/+", "a/x/y"));
    TEST_ASSERT_FALSE(match("a/+", "a"));
    TEST_ASSERT_TRUE(match("+/b", "a/b"));
    TEST_ASSERT_TRUE(match("+", "a"));
    TEST_ASSERT_FALSE(match("+", "a/b"));
    TEST_ASSERT_TRUE(match("buttonplus/btn/button/+/label", "buttonplus/btn/button/2/label"));
    TEST_ASSERT_FALSE(match("buttonplus/btn/button/+/label", "buttonplus/btn/button/2/top_label"));
    TEST_ASSERT_FALSE(match("buttonplus/btn/button/+/label", "buttonplus/btn/button/2/click"));
}

void test_plus_with_hash() {
    TEST_ASSERT_TRUE(match("a/+/#", "a/x"));
    TEST_ASSERT_TRUE(match("a/+/#", "a/x/y/z"));
    TEST_ASSERT_FALSE(match("a/+/#", "a"));
    TEST_ASSERT_TRUE(match("+/#", "a"));
}

void test_wildcard_not_a_whole_level_is_literal() {
    TEST_ASSERT_TRUE(match("a/b+", "a/b+"));
    TEST_ASSERT_FALSE(match("a/b+", "a/bc"));
    TEST_ASSERT_TRUE(match("a/#b", "a/#b"));
    TEST_ASSERT_FALSE(match("a/#b", "a/xb"));
}

void test_dollar_topics_and_length_bound_with_plus() {
    TEST_ASSERT_FALSE(match("#", "$SYS/x"));
    TEST_ASSERT_FALSE(match("+/x", "$SYS/x"));
    TEST_ASSERT_TRUE(match("$SYS/#", "$SYS/x"));
    const char buf[] = "a/b/cXYZ";  // only the first 5 bytes are the topic
    TEST_ASSERT_TRUE(mqtt_topic_matches("a/+/c", buf, 5));
    TEST_ASSERT_TRUE(mqtt_topic_matches("a/b/+", buf, 5));
    TEST_ASSERT_FALSE(mqtt_topic_matches("a/b/+/+", buf, 5));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_exact);
    RUN_TEST(test_length_bounded_topic);
    RUN_TEST(test_hash_level_boundary);
    RUN_TEST(test_hash_alone_and_leading_slash);
    RUN_TEST(test_plus_single_level);
    RUN_TEST(test_plus_with_hash);
    RUN_TEST(test_wildcard_not_a_whole_level_is_literal);
    RUN_TEST(test_dollar_topics_and_length_bound_with_plus);
    return UNITY_END();
}
