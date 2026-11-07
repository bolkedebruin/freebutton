//
// Native unit tests for HaCompact: abbreviated + "~"-based discovery payloads
// must expand (Home Assistant's rules) back to exactly the original config.
//
#include <unity.h>
#include <map>
#include <string>

#include "../../src/mqtt/HaCompact.cpp"

using namespace freebutton::ha;

void setUp() {}
void tearDown() {}

// Real light config as published by the firmware (from a device log).
static const char* kLight = R"({"name":"Button 7 wall LED","default_entity_id":"light.buttonplus_test_led_7_1","state_topic":"buttonplus/btn_a00002/button7_wall/led/state","command_topic":"buttonplus/btn_a00002/button7_wall/led/set","brightness_command_topic":"buttonplus/btn_a00002/button7_wall/led/brightness/set","brightness_scale":"255","brightness_state_topic":"buttonplus/btn_a00002/button7_wall/led/brightness/state","effect_command_topic":"buttonplus/btn_a00002/button7_wall/led/effect/set","effect_list":"[\"none\",\"glow\",\"flash\"]","effect_state_topic":"buttonplus/btn_a00002/button7_wall/led/effect/state","retain":"true","rgb_command_template":"{{ {\"r\": red, \"g\": green, \"b\": blue} | tojson }}","rgb_command_topic":"buttonplus/btn_a00002/button7_wall/led/color/set","rgb_state_topic":"buttonplus/btn_a00002/button7_wall/led/color/state","rgb_value_template":"{{ value_json.r }},{{ value_json.g }},{{ value_json.b }}","schema":"json","unique_id":"btn_a00002_led_7_1","device":{"name":"ButtonPlus TEST","manufacturer":"ButtonPlus","model":"Model 1 Version 9","sw_version":"FreeButton 3.1.0-alpha.1","identifiers":["btn_a00002"]}})";

static const char* kEvent = R"({"name":"Button 7","default_entity_id":"event.buttonplus_test_button_7","state_topic":"buttonplus/btn_a00002/button_7/state","device_class":"button","event_types":["click","longpress"],"availability_topic":"buttonplus/btn_a00002/status","payload_available":"online","payload_not_available":"offline","unique_id":"btn_a00002_button_7_event","device":{"name":"ButtonPlus TEST","manufacturer":"ButtonPlus","model":"Model 1 Version 9","sw_version":"FreeButton 3.1.0-alpha.1","identifiers":["btn_a00002"]}})";


// Home Assistant's expansion: abbreviations first, then "~" in *topic keys.
static JsonDocument expandLikeHomeAssistant(const JsonDocument& compact) {
    std::map<std::string, std::string> full, devFull;
    for (const char* k : {"availability_topic","payload_available","payload_not_available","brightness_command_topic",
                          "brightness_state_topic","brightness_scale","command_topic","state_topic","effect_command_topic",
                          "effect_state_topic","effect_list","rgb_command_template","rgb_command_topic","rgb_state_topic",
                          "rgb_value_template","retain","unique_id","device_class","unit_of_measurement","value_template",
                          "event_types","default_entity_id","device"}) {
        full[std::string(abbreviateKey(k))] = k;
    }
    for (const char* k : {"identifiers","manufacturer","model","sw_version","name"}) {
        devFull[std::string(abbreviateDeviceKey(k))] = k;
    }

    JsonDocument out;
    std::string base;
    if (compact["~"].is<const char*>()) base = compact["~"].as<std::string>();

    for (JsonPairConst kv : compact.as<JsonObjectConst>()) {
        std::string key = kv.key().c_str();
        if (key == "~") continue;
        if (full.count(key)) key = full[key];
        if (key == "device") {
            JsonObject dev = out[key].to<JsonObject>();
            for (JsonPairConst d : kv.value().as<JsonObjectConst>()) {
                std::string dk = d.key().c_str();
                dev[devFull.count(dk) ? devFull[dk] : dk] = d.value();
            }
            continue;
        }
        if (!base.empty() && kv.value().is<const char*>() && key.size() >= 5 &&
            key.compare(key.size() - 5, 5, "topic") == 0) {
            std::string v = kv.value().as<std::string>();
            if (!v.empty() && v[0] == '~') { out[key] = base + v.substr(1); continue; }
        }
        out[key] = kv.value();
    }
    return out;
}

static void roundTrip(const char* json, size_t maxCompactBytes) {
    JsonDocument original;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(original, json).code());

    const JsonDocument compact = compactDiscovery(original);
    std::string compactStr; serializeJson(compact, compactStr);
    TEST_ASSERT_LESS_OR_EQUAL(maxCompactBytes, compactStr.size());
    TEST_ASSERT_TRUE(compactStr.size() < strlen(json));

    const JsonDocument expanded = expandLikeHomeAssistant(compact);
    // Compare as maps (key order may differ)
    TEST_ASSERT_EQUAL(original.as<JsonObjectConst>().size(), expanded.as<JsonObjectConst>().size());
    for (JsonPairConst kv : original.as<JsonObjectConst>()) {
        std::string a, b;
        serializeJson(kv.value(), a);
        serializeJson(expanded[kv.key()], b);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(a.c_str(), b.c_str(), kv.key().c_str());
    }
}

void test_light_round_trip_and_size() { roundTrip(kLight, 720); }
void test_event_round_trip_and_size() { roundTrip(kEvent, 410); }

void test_common_base() {
    JsonDocument d;
    d["state_topic"] = "buttonplus/btn_a00002/button7_wall/led/state";
    d["command_topic"] = "buttonplus/btn_a00002/button7_wall/led/brightness/set";
    TEST_ASSERT_EQUAL_STRING("buttonplus/btn_a00002/button7_wall/led", commonTopicBase(d).c_str());
    d["availability_topic"] = "buttonplus/btn_a00002/status";
    TEST_ASSERT_EQUAL_STRING("buttonplus/btn_a00002", commonTopicBase(d).c_str());
    // Not cut mid-segment: "button7_front" vs "button7_wall" -> "buttonplus/btn_a00002"
    JsonDocument e;
    e["state_topic"] = "buttonplus/btn_a00002/button7_front/x";
    e["command_topic"] = "buttonplus/btn_a00002/button7_wall/y";
    TEST_ASSERT_EQUAL_STRING("buttonplus/btn_a00002", commonTopicBase(e).c_str());
    JsonDocument f;
    f["state_topic"] = "a/x";
    f["command_topic"] = "b/y";
    TEST_ASSERT_EQUAL_STRING("", commonTopicBase(f).c_str());
}

void test_base_topic_only_on_topic_keys() {
    JsonDocument d;
    d["name"] = "buttonplus/btn_a00002/x/not-a-topic";
    d["state_topic"] = "buttonplus/btn_a00002/x/state";
    d["command_topic"] = "buttonplus/btn_a00002/x/set";
    const JsonDocument c = compactDiscovery(d);
    TEST_ASSERT_EQUAL_STRING("buttonplus/btn_a00002/x/not-a-topic", c["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("~/state", c["stat_t"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("~/set", c["cmd_t"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("buttonplus/btn_a00002/x", c["~"].as<const char*>());
}

void test_no_base_key_when_unused() {
    JsonDocument d;
    d["name"] = "x";
    const JsonDocument c = compactDiscovery(d);
    TEST_ASSERT_TRUE(c["~"].isNull());
}

void test_unknown_keys_kept() {
    TEST_ASSERT_EQUAL_STRING("schema", std::string(abbreviateKey("schema")).c_str());
    TEST_ASSERT_EQUAL_STRING("name", std::string(abbreviateDeviceKey("name")).c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_light_round_trip_and_size);
    RUN_TEST(test_event_round_trip_and_size);
    RUN_TEST(test_common_base);
    RUN_TEST(test_base_topic_only_on_topic_keys);
    RUN_TEST(test_no_base_key_when_unused);
    RUN_TEST(test_unknown_keys_kept);
    return UNITY_END();
}
