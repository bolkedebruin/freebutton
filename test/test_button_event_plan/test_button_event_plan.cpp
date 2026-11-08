//
// Native unit tests for ButtonEventPlan: which keys get a Home Assistant
// `event` entity, its event_types, and when the firmware mirrors presses to
// the canonical buttonplus/<id>/button_<pos>/state topic.
//
// The .cpp is included directly (same pattern as test_deviceinfo) so no
// build_src_filter change is needed.
//
#include <unity.h>
#include <string>
#include <vector>

#include "../../src/components/ButtonEventPlan.cpp"

using namespace freebutton::ha;

static const std::string kDev = "btn_a00003";

void setUp() {}
void tearDown() {}

static bool hasType(const ButtonEventPlan& plan, const char* type) {
    for (const auto& t : plan.eventTypes) {
        if (t == type) return true;
    }
    return false;
}

void test_canonical_topic_format() {
    TEST_ASSERT_EQUAL_STRING("buttonplus/btn_a00003/button_7/state", canonicalButtonStateTopic(kDev, 7).c_str());
    TEST_ASSERT_EQUAL_STRING("buttonplus/btn_a00003/button_7/set", buttonCommandTopic(kDev, 7).c_str());
}

void test_json_event_type() {
    TEST_ASSERT_TRUE(jsonEventType(R"({"event_type":"click"})").has_value());
    TEST_ASSERT_EQUAL_STRING("longpress", jsonEventType(R"({"event_type": "longpress", "x": 1})")->c_str());
    TEST_ASSERT_FALSE(jsonEventType("press").has_value());
    TEST_ASSERT_FALSE(jsonEventType("").has_value());
    TEST_ASSERT_FALSE(jsonEventType(R"(["click"])").has_value());
    TEST_ASSERT_FALSE(jsonEventType(R"({"event_type":""})").has_value());
    TEST_ASSERT_FALSE(jsonEventType(R"({"event_type":3})").has_value());
}

// The heap-free scanner behind jsonEventType: strict JSON, nested values
// skipped, escapes decoded, last duplicate wins, trailing content ignored.
void test_json_event_type_scanner() {
    TEST_ASSERT_EQUAL_STRING("x", jsonEventType(R"({"a":{"event_type":"no"},"b":[1,-2.5e3,true,null,"s"],"event_type":"x"})")->c_str());
    TEST_ASSERT_EQUAL_STRING("a\"b/\xC3\xA9", jsonEventType(R"({"event_type":"a\"b\/é"})")->c_str());
    TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x98\x80", jsonEventType(R"({"event_type":"😀"})")->c_str());
    TEST_ASSERT_EQUAL_STRING("two", jsonEventType(R"({"event_type":"one","event_type":"two"})")->c_str());
    TEST_ASSERT_FALSE(jsonEventType(R"({"event_type":"one","event_type":2})").has_value());
    TEST_ASSERT_EQUAL_STRING("click", jsonEventType(R"(  {"event_type" : "click"}  trailing)")->c_str());
    TEST_ASSERT_EQUAL_STRING("click", jsonEventType(R"({"event_type":"click"})")->c_str());
    TEST_ASSERT_FALSE(jsonEventType(R"({"event_type":"click")").has_value());     // unterminated
    TEST_ASSERT_FALSE(jsonEventType(R"({"event_type":"click",})").has_value());   // trailing comma
    TEST_ASSERT_FALSE(jsonEventType(R"({"event_type":"click":1})").has_value());
    TEST_ASSERT_FALSE(jsonEventType(R"({"a":[1 2],"event_type":"click"})").has_value());
    TEST_ASSERT_FALSE(jsonEventType(R"({"a":{"b"},"event_type":"click"})").has_value());
    TEST_ASSERT_FALSE(jsonEventType(R"({"a":[1,],"event_type":"click"})").has_value());
    TEST_ASSERT_FALSE(jsonEventType(R"({"a":abc,"event_type":"click"})").has_value());  // unquoted value
    TEST_ASSERT_FALSE(jsonEventType(R"({"a":[1}],"event_type":"click"})").has_value()); // mismatched
    // ArduinoJson's nesting limit (10 incl. the top object), without recursion.
    TEST_ASSERT_TRUE(jsonEventType(R"({"a":[[[[[[[[[1]]]]]]]]],"event_type":"c"})").has_value());     // 9 nested
    TEST_ASSERT_FALSE(jsonEventType(R"({"a":[[[[[[[[[[1]]]]]]]]]],"event_type":"c"})").has_value());  // 10 nested
    TEST_ASSERT_TRUE(jsonEventType(R"({"a":{"b":{"c":[{"d":[]},{}]}},"event_type":"c"})").has_value());
    TEST_ASSERT_FALSE(jsonEventType(R"({"a":)" + std::string(100000, '[')).has_value());  // bounded, no recursion
}

// What ArduinoJson accepted keeps working (a config that announced an event
// entity before must still do so): single quotes, unquoted keys, loose numbers.
void test_json_event_type_arduinojson_compat() {
    TEST_ASSERT_EQUAL_STRING("click", jsonEventType(R"({'event_type':'click'})")->c_str());
    TEST_ASSERT_EQUAL_STRING("it's", jsonEventType(R"({"event_type":'it\'s'})")->c_str());
    TEST_ASSERT_EQUAL_STRING("click", jsonEventType(R"({event_type:"click"})")->c_str());
    TEST_ASSERT_EQUAL_STRING("click", jsonEventType(R"({x_1:{'y':[1]},"event_type":"click"})")->c_str());
    TEST_ASSERT_EQUAL_STRING("click", jsonEventType(R"({"x":01,"y":-1.5E+3,"event_type":"click"})")->c_str());
    TEST_ASSERT_EQUAL_STRING("a\tb", jsonEventType("{\"event_type\":\"a\tb\"}")->c_str());  // raw control char
}

void test_unannounced_plan() {
    const auto plan = unannouncedPlan("canonical press topic does not fit its buffer");
    TEST_ASSERT_FALSE(plan.announceEvent);
    TEST_ASSERT_FALSE(plan.mirrorClick);
    TEST_ASSERT_FALSE(plan.mirrorLongPress);
    TEST_ASSERT_EQUAL_STRING("canonical press topic does not fit its buffer", plan.problem.c_str());
}

void test_json_event_type_buffer() {
    char out[8];
    size_t length = 0;
    TEST_ASSERT_TRUE(jsonEventType(R"({"event_type":"click"})", out, sizeof(out), length));
    TEST_ASSERT_EQUAL(5, length);
    TEST_ASSERT_EQUAL_STRING("click", out);
    TEST_ASSERT_FALSE(jsonEventType(R"({"event_type":"longpress"})", out, sizeof(out), length));  // does not fit
}

// An event_type longer than EventTypeList::kMaxLength cannot be listed: the
// key is not announced (with a problem) instead of announcing a wrong list.
void test_too_long_event_type_not_announced() {
    const auto canon = canonicalButtonStateTopic(kDev, 8);
    const std::string payload = R"({"event_type":")" + std::string(EventTypeList::kMaxLength + 1, 'x') + R"("})";
    const std::vector<PressTopic> topics = {{0, canon, payload, CLICK}};
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_FALSE(plan.announceEvent);
    TEST_ASSERT_FALSE(plan.problem.empty());
}

void test_canonical_topic_buffer() {
    char buf[64];
    TEST_ASSERT_EQUAL(36, canonicalButtonStateTopic(buf, sizeof(buf), kDev, 7));
    TEST_ASSERT_EQUAL_STRING("buttonplus/btn_a00003/button_7/state", buf);
    char small[10];
    TEST_ASSERT_EQUAL(0, canonicalButtonStateTopic(small, sizeof(small), kDev, 7));
}

// btn_a00003: FreeButton default config, empty payloads on the canonical topic.
void test_default_config_no_mirror() {
    const auto canon = canonicalButtonStateTopic(kDev, 7);
    const std::vector<PressTopic> topics = {
        {0, canon, "", CLICK},
        {0, canon, "", LONG_PRESS},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_FALSE(plan.mirrorClick);
    TEST_ASSERT_FALSE(plan.mirrorLongPress);
    TEST_ASSERT_EQUAL(2, plan.eventTypes.size());
    TEST_ASSERT_TRUE(hasType(plan, "click"));
    TEST_ASSERT_TRUE(hasType(plan, "longpress"));
}

// Explicit default payloads behave like empty ones.
void test_explicit_default_payload_no_mirror() {
    const auto canon = canonicalButtonStateTopic(kDev, 7);
    const std::vector<PressTopic> topics = {
        {0, canon, CLICK_PAYLOAD, CLICK},
        {0, canon, LONG_PRESS_PAYLOAD, LONG_PRESS},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_FALSE(plan.mirrorClick);
    TEST_ASSERT_FALSE(plan.mirrorLongPress);
}

// A release topic on the canonical topic adds "release" to event_types.
void test_release_on_canonical_listed() {
    const auto canon = canonicalButtonStateTopic(kDev, 7);
    const std::vector<PressTopic> topics = {
        {0, canon, "", CLICK},
        {0, canon, "", LONG_PRESS},
        {0, canon, "", RELEASE},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_EQUAL(3, plan.eventTypes.size());
    TEST_ASSERT_TRUE(hasType(plan, "release"));
}

// btn_a00001: legacy ha-button-plus topics with payload "press" -> mirror both.
void test_legacy_config_mirrors() {
    const auto canon = canonicalButtonStateTopic("btn_a00001", 7);
    const std::vector<PressTopic> topics = {
        {0, "buttonplus/btn_a00001/button/6/click", "press", CLICK},
        {0, "buttonplus/btn_a00001/button/6/long_press", "press", LONG_PRESS},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_TRUE(plan.mirrorClick);
    TEST_ASSERT_TRUE(plan.mirrorLongPress);
    TEST_ASSERT_EQUAL(2, plan.eventTypes.size());
}

// Key configured but with no press topics at all -> still announced, mirrored.
void test_no_press_topics_mirrors() {
    const auto canon = canonicalButtonStateTopic(kDev, 3);
    const std::vector<PressTopic> topics = {
        {0, "some/label/topic", "", LABEL},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_TRUE(plan.mirrorClick);
    TEST_ASSERT_TRUE(plan.mirrorLongPress);
}

// Physical key with no config Button at all: announced, both presses mirrored.
void test_unconfigured_key_mirrors() {
    const auto canon = canonicalButtonStateTopic(kDev, 5);
    const auto plan = planButtonEvents({}, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_TRUE(plan.mirrorClick);
    TEST_ASSERT_TRUE(plan.mirrorLongPress);
    TEST_ASSERT_EQUAL(2, plan.eventTypes.size());
}

// Mixed: click on canonical with default, long press on a custom topic.
void test_mixed_mirrors_only_missing() {
    const auto canon = canonicalButtonStateTopic(kDev, 8);
    const std::vector<PressTopic> topics = {
        {0, canon, "", CLICK},
        {0, "custom/long", "x", LONG_PRESS},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_FALSE(plan.mirrorClick);
    TEST_ASSERT_TRUE(plan.mirrorLongPress);
}

// Canonical topic but on another broker is not "covered" for the HA broker.
void test_other_broker_mirrors() {
    const auto canon = canonicalButtonStateTopic(kDev, 8);
    const std::vector<PressTopic> topics = {
        {1, canon, "", CLICK},
        {1, canon, "", LONG_PRESS},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_TRUE(plan.mirrorClick);
    TEST_ASSERT_TRUE(plan.mirrorLongPress);
}

// A custom JSON event_type on the canonical topic is listed, and the default
// click is still mirrored (so "click" stays valid too).
void test_custom_json_event_type_listed() {
    const auto canon = canonicalButtonStateTopic(kDev, 8);
    const std::vector<PressTopic> topics = {
        {0, canon, R"({"event_type":"single"})", CLICK},
        {0, canon, "", LONG_PRESS},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_TRUE(plan.mirrorClick);
    TEST_ASSERT_FALSE(plan.mirrorLongPress);
    TEST_ASSERT_TRUE(hasType(plan, "single"));
    TEST_ASSERT_TRUE(hasType(plan, "click"));
    TEST_ASSERT_TRUE(hasType(plan, "longpress"));
}

// Non-JSON payload on the canonical topic -> HA would reject it; don't announce.
void test_non_json_on_canonical_not_announced() {
    const auto canon = canonicalButtonStateTopic(kDev, 8);
    const std::vector<PressTopic> topics = {
        {0, canon, "press", CLICK},
        {0, canon, "", LONG_PRESS},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_FALSE(plan.announceEvent);
    TEST_ASSERT_FALSE(plan.mirrorClick);
    TEST_ASSERT_FALSE(plan.mirrorLongPress);
    TEST_ASSERT_FALSE(plan.problem.empty());
}

// Only the FIRST topic per event type is published (Button::publishToTopic).
void test_only_first_topic_per_event_counts() {
    const auto canon = canonicalButtonStateTopic(kDev, 8);
    const std::vector<PressTopic> topics = {
        {0, canon, "", CLICK},
        {0, canon, "press", CLICK},   // never published
        {0, canon, "", LONG_PRESS},
    };
    const auto plan = planButtonEvents(topics, 0, canon);
    TEST_ASSERT_TRUE(plan.announceEvent);
    TEST_ASSERT_FALSE(plan.mirrorClick);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_canonical_topic_format);
    RUN_TEST(test_json_event_type);
    RUN_TEST(test_default_config_no_mirror);
    RUN_TEST(test_explicit_default_payload_no_mirror);
    RUN_TEST(test_release_on_canonical_listed);
    RUN_TEST(test_legacy_config_mirrors);
    RUN_TEST(test_no_press_topics_mirrors);
    RUN_TEST(test_unconfigured_key_mirrors);
    RUN_TEST(test_mixed_mirrors_only_missing);
    RUN_TEST(test_other_broker_mirrors);
    RUN_TEST(test_custom_json_event_type_listed);
    RUN_TEST(test_non_json_on_canonical_not_announced);
    RUN_TEST(test_only_first_topic_per_event_counts);
    RUN_TEST(test_json_event_type_scanner);
    RUN_TEST(test_json_event_type_arduinojson_compat);
    RUN_TEST(test_json_event_type_buffer);
    RUN_TEST(test_unannounced_plan);
    RUN_TEST(test_too_long_event_type_not_announced);
    RUN_TEST(test_canonical_topic_buffer);
    return UNITY_END();
}
