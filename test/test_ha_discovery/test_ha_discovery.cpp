//
// Native unit tests for the Home Assistant discovery payloads (HaEntities).
//
// The firmware writes HA's abbreviated keys and "~" topics directly. Each
// payload is expanded here exactly the way Home Assistant does it
// (homeassistant/components/mqtt/discovery.py: _replace_all_abbreviations,
// then _replace_topic_base) and must equal the full config earlier firmware
// published (kLight / kEvent are from a device log).
//
// The .cpp files are included directly (same pattern as test_deviceinfo).
//
#include <unity.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "../../src/mqtt/HaSlug.cpp"
#include "../../src/mqtt/HaEntities.cpp"
#include "ha_abbreviations.h"

using namespace freebutton::ha;

void setUp() {}
void tearDown() {}

// --- Fixtures: full configs of the earlier firmware ---------------------------

#define DEV_FULL R"("device":{"name":"ButtonPlus TEST","manufacturer":"ButtonPlus","model":"Model 1 Version 9","sw_version":"FreeButton 3.1.0-alpha.1","identifiers":["btn_a00002"]})"

// Device log, except effect_list: earlier firmware sent it as a JSON-encoded
// STRING ("[\"none\",...]"); it is a real array now (bug fix).
static const char* kLight = R"({"name":"Button 7 wall LED","default_entity_id":"light.buttonplus_test_led_7_1","state_topic":"buttonplus/btn_a00002/button7_wall/led/state","command_topic":"buttonplus/btn_a00002/button7_wall/led/set","brightness_command_topic":"buttonplus/btn_a00002/button7_wall/led/brightness/set","brightness_scale":"255","brightness_state_topic":"buttonplus/btn_a00002/button7_wall/led/brightness/state","effect_command_topic":"buttonplus/btn_a00002/button7_wall/led/effect/set","effect_list":["none","glow","flash"],"effect_state_topic":"buttonplus/btn_a00002/button7_wall/led/effect/state","retain":"true","rgb_command_template":"{{ {\"r\": red, \"g\": green, \"b\": blue} | tojson }}","rgb_command_topic":"buttonplus/btn_a00002/button7_wall/led/color/set","rgb_state_topic":"buttonplus/btn_a00002/button7_wall/led/color/state","rgb_value_template":"{{ value_json.r }},{{ value_json.g }},{{ value_json.b }}","schema":"json","unique_id":"btn_a00002_led_7_1",)" DEV_FULL "}";

static const char* kEvent = R"({"name":"Button 7","default_entity_id":"event.buttonplus_test_button_7","state_topic":"buttonplus/btn_a00002/button_7/state","device_class":"button","event_types":["click","longpress"],"availability_topic":"buttonplus/btn_a00002/status","payload_available":"online","payload_not_available":"offline","unique_id":"btn_a00002_button_7_event",)" DEV_FULL "}";

// HomeAssistantDiscovery::configToJson of the earlier firmware for the button entity.
static const char* kButton = R"({"name":"Button 7","default_entity_id":"button.buttonplus_test_button_7","command_topic":"buttonplus/btn_a00002/button_7/set","availability_topic":"buttonplus/btn_a00002/status","payload_available":"online","payload_not_available":"offline","unique_id":"btn_a00002_button_7",)" DEV_FULL "}";

// triggerToJson of the earlier firmware (it was not compacted).
static const char* kClickTrigger = R"({"automation_type":"trigger","type":"button_short_press","subtype":"button_7","topic":"buttonplus/btn_a00002/button_7/state","payload":"{\"event_type\":\"click\"}",)" DEV_FULL "}";
static const char* kLongTrigger = R"({"automation_type":"trigger","type":"button_long_press","subtype":"button_7","topic":"buttonplus/btn_a00002/button_7/state","payload":"{\"event_type\":\"longpress\"}",)" DEV_FULL "}";

// Sensors.cpp registerSensorsWithDiscovery of the earlier firmware.
static const char* kSensor = R"({"name":"STS35 Temperature 1","default_entity_id":"sensor.buttonplus_test_sts35_1_74","state_topic":"buttonplus/btn_a00002/sensor/sts35_1/state","device_class":"temperature","unit_of_measurement":"°C","unique_id":"btn_a00002_sts35_1_74",)" DEV_FULL "}";
// device_class / unit omitted when empty.
static const char* kVirtualSensor = R"({"name":"Free Memory","default_entity_id":"sensor.buttonplus_test_free_memory_100","state_topic":"buttonplus/btn_a00002/sensor/free_memory/state","unique_id":"btn_a00002_free_memory_100",)" DEV_FULL "}";

static const DiscoveryDevice kDevice{"ButtonPlus TEST", "ButtonPlus", "Model 1 Version 9", "FreeButton 3.1.0-alpha.1",
                                     "btn_a00002"};

// --- Home Assistant's expansion -------------------------------------------------

template <size_t N>
static std::string_view expandKey(const ha_abbreviations::Entry (&table)[N], const std::string_view key) {
    for (const auto& [abbr, full] : table) {
        if (abbr == key) return full;
    }
    return key;
}

static bool endsWith(const std::string_view s, const std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

// _replace_all_abbreviations + _replace_topic_base.
static JsonDocument expandLikeHomeAssistant(const JsonDocument& compact) {
    JsonDocument out;
    for (const JsonPairConst kv : compact.as<JsonObjectConst>()) {
        const std::string full(expandKey(ha_abbreviations::kAbbreviations, kv.key().c_str()));
        TEST_ASSERT_TRUE_MESSAGE(out[full].isNull(), ("two keys expand to " + full).c_str());
        if (full == "device" && kv.value().is<JsonObjectConst>()) {
            const JsonObject dev = out[full].to<JsonObject>();
            for (const JsonPairConst dkv : kv.value().as<JsonObjectConst>()) {
                dev[std::string(expandKey(ha_abbreviations::kDeviceAbbreviations, dkv.key().c_str()))] = dkv.value();
            }
        } else {
            out[full] = kv.value();
        }
    }
    if (!out["~"].isNull()) {
        const std::string base = out["~"].as<std::string>();
        out.remove("~");
        for (JsonPair kv : out.as<JsonObject>()) {
            if (!kv.value().is<const char*>()) continue;
            const std::string value = kv.value().as<std::string>();
            if (value.empty() || !endsWith(kv.key().c_str(), "topic")) continue;
            std::string replaced = value;
            if (value.front() == '~') replaced = base + value.substr(1);
            if (value.back() == '~') replaced = value.substr(0, value.size() - 1) + base;
            kv.value().set(replaced);
        }
    }
    return out;
}

// Order-independent text form of a JSON value.
static std::string canonical(const JsonVariantConst v) {
    if (v.is<JsonObjectConst>()) {
        std::vector<std::string> keys;
        for (const JsonPairConst kv : v.as<JsonObjectConst>()) keys.emplace_back(kv.key().c_str());
        std::sort(keys.begin(), keys.end());
        std::string s = "{";
        for (const auto& k : keys) {
            if (s.size() > 1) s += ",";
            s += "\"" + k + "\":" + canonical(v[k]);
        }
        return s + "}";
    }
    if (v.is<JsonArrayConst>()) {
        std::string s = "[";
        for (const JsonVariantConst item : v.as<JsonArrayConst>()) {
            if (s.size() > 1) s += ",";
            s += canonical(item);
        }
        return s + "]";
    }
    std::string s;
    serializeJson(v, s);
    return s;
}

// --- Helpers ----------------------------------------------------------------------

struct Message {
    std::string topic;
    std::string payload;
    bool ok = false;
};

template <typename Write>
static Message generate(Write write, const DiscoveryDevice& device = kDevice, size_t payloadCapacity = 1024) {
    char topic[128];
    std::vector<char> payload(payloadCapacity);
    DiscoveryOut out(topic, sizeof(topic), payload.data(), payload.size(), "homeassistant", device);
    write(out);
    size_t topicLength = 0;
    size_t payloadLength = 0;
    Message m;
    m.ok = out.finish(topicLength, payloadLength);
    if (m.ok) {
        TEST_ASSERT_EQUAL(topicLength, std::strlen(topic));
        TEST_ASSERT_EQUAL(payloadLength, std::strlen(payload.data()));
        m.topic.assign(topic, topicLength);
        m.payload.assign(payload.data(), payloadLength);
    }
    return m;
}

static void assertExpandsTo(const char* expectedFull, const Message& m) {
    TEST_ASSERT_TRUE(m.ok);
    JsonDocument compact;
    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, deserializeJson(compact, m.payload).code(), m.payload.c_str());
    JsonDocument expected;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(expected, expectedFull).code());
    TEST_ASSERT_EQUAL_STRING(canonical(expected.as<JsonVariantConst>()).c_str(),
                             canonical(expandLikeHomeAssistant(compact).as<JsonVariantConst>()).c_str());
}

// --- Tests --------------------------------------------------------------------------

void test_event() {
    constexpr std::string_view types[] = {"click", "longpress"};
    const auto m = generate([&](DiscoveryOut& out) { writeButtonEvent(out, 7, true, types, 2); });
    TEST_ASSERT_EQUAL_STRING("homeassistant/event/btn_a00002/button_7/config", m.topic.c_str());
    assertExpandsTo(kEvent, m);
}

void test_button() {
    const auto m = generate([](DiscoveryOut& out) { writeButtonCommand(out, 7, true); });
    TEST_ASSERT_EQUAL_STRING("homeassistant/button/btn_a00002/button_7/config", m.topic.c_str());
    assertExpandsTo(kButton, m);
}

void test_triggers() {
    const auto click = generate([](DiscoveryOut& out) { writeButtonTrigger(out, 7, false, true); });
    TEST_ASSERT_EQUAL_STRING("homeassistant/device_automation/btn_a00002/button_7_click/config", click.topic.c_str());
    assertExpandsTo(kClickTrigger, click);

    const auto longPress = generate([](DiscoveryOut& out) { writeButtonTrigger(out, 7, true, true); });
    TEST_ASSERT_EQUAL_STRING("homeassistant/device_automation/btn_a00002/button_7_long/config", longPress.topic.c_str());
    assertExpandsTo(kLongTrigger, longPress);
}

void test_light() {
    const auto m = generate([](DiscoveryOut& out) {
        writeLight(out, 7, 1, "wall", "buttonplus/btn_a00002/button7_wall");
    });
    TEST_ASSERT_EQUAL_STRING("homeassistant/light/btn_a00002/led_7_1/config", m.topic.c_str());
    assertExpandsTo(kLight, m);
}

void test_sensors() {
    const auto m = generate([](DiscoveryOut& out) {
        writeSensor(out, "sts35_1_74", "STS35 Temperature 1", "temperature", "\xC2\xB0" "C", "sts35_1");
    });
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/btn_a00002/sts35_1_74/config", m.topic.c_str());
    assertExpandsTo(kSensor, m);

    const auto v = generate([](DiscoveryOut& out) {
        writeSensor(out, "free_memory_100", "Free Memory", "", "", "free_memory");
    });
    assertExpandsTo(kVirtualSensor, v);
}

// Missing hardware / not announced: empty retained payload on the same topic.
void test_remove() {
    const auto event = generate([](DiscoveryOut& out) { writeButtonEvent(out, 3, false, nullptr, 0); });
    TEST_ASSERT_TRUE(event.ok);
    TEST_ASSERT_EQUAL_STRING("homeassistant/event/btn_a00002/button_3/config", event.topic.c_str());
    TEST_ASSERT_EQUAL(0, event.payload.size());

    const auto button = generate([](DiscoveryOut& out) { writeButtonCommand(out, 3, false); });
    TEST_ASSERT_EQUAL_STRING("homeassistant/button/btn_a00002/button_3/config", button.topic.c_str());
    TEST_ASSERT_EQUAL(0, button.payload.size());

    const auto trigger = generate([](DiscoveryOut& out) { writeButtonTrigger(out, 3, true, false); });
    TEST_ASSERT_EQUAL_STRING("homeassistant/device_automation/btn_a00002/button_3_long/config", trigger.topic.c_str());
    TEST_ASSERT_EQUAL(0, trigger.payload.size());
}

// No friendly name: device name and default_entity_id fall back to the id.
void test_unnamed_device() {
    const DiscoveryDevice device{"", "ButtonPlus", "Model 1 Version 9", "FreeButton 3.1.0-alpha.1", "btn_a00002"};
    const auto m = generate([](DiscoveryOut& out) { writeButtonCommand(out, 2, true); }, device);
    JsonDocument doc;
    deserializeJson(doc, m.payload);
    const auto full = expandLikeHomeAssistant(doc);
    TEST_ASSERT_EQUAL_STRING("button.btn_a00002_button_2", full["default_entity_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("btn_a00002", full["device"]["name"].as<const char*>());
}

// Friendly name slug, as before: "Café Één" -> cafe_een.
void test_slugged_name() {
    const DiscoveryDevice device{"Caf\xC3\xA9 \xC3\x89\xC3\xA9n", "", "", "", "btn_a00002"};
    const auto m = generate([](DiscoveryOut& out) { writeButtonCommand(out, 2, true); }, device);
    JsonDocument doc;
    deserializeJson(doc, m.payload);
    const auto full = expandLikeHomeAssistant(doc);
    TEST_ASSERT_EQUAL_STRING("button.cafe_een_button_2", full["default_entity_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Caf\xC3\xA9 \xC3\x89\xC3\xA9n", full["device"]["name"].as<const char*>());
    TEST_ASSERT_TRUE(full["device"]["manufacturer"].isNull());  // empty fields omitted
}

// A payload that does not fit is never published (not even truncated / empty).
void test_overflow_not_published() {
    const auto m = generate([](DiscoveryOut& out) {
        writeLight(out, 7, 1, "wall", "buttonplus/btn_a00002/button7_wall");
    }, kDevice, 200);
    TEST_ASSERT_FALSE(m.ok);
}

// A name / id / topic that would be cut, or a missing "~" base, fails the
// entry instead of announcing a shortened one.
void test_cut_or_missing_parts_not_published() {
    const std::string longPosition(60, 'p');
    char topic[128];
    char payload[1024];
    {
        DiscoveryOut out(topic, sizeof(topic), payload, sizeof(payload), "homeassistant", kDevice);
        writeLight(out, 7, 1, longPosition, "buttonplus/btn_a00002/button7_wall");  // label > 48 B
        size_t t, p;
        TEST_ASSERT_FALSE(out.finish(t, p));
        TEST_ASSERT_TRUE(out.failed());
    }
    {
        DiscoveryOut out(topic, sizeof(topic), payload, sizeof(payload), "homeassistant", kDevice);
        writeLight(out, 7, 1, "wall", "");  // no base for the "~/..." topics
        size_t t, p;
        TEST_ASSERT_FALSE(out.finish(t, p));
        TEST_ASSERT_TRUE(out.failed());
    }
    {
        const std::string longId(70, 'i');  // "buttonplus/<id>" does not fit the 64 B base
        const DiscoveryDevice device{"x", "", "", "", longId};
        DiscoveryOut out(topic, sizeof(topic), payload, sizeof(payload), "homeassistant", device);
        writeButtonCommand(out, 1, true);
        size_t t, p;
        TEST_ASSERT_FALSE(out.finish(t, p));
        TEST_ASSERT_TRUE(out.failed());
    }
    {
        DiscoveryOut out(topic, sizeof(topic), payload, sizeof(payload), "homeassistant", kDevice);
        writeSensor(out, "", "Nameless", "", "", "x");
        size_t t, p;
        TEST_ASSERT_FALSE(out.finish(t, p));
    }
}

// Without a device id there is no valid topic: nothing is published.
void test_no_identifier_not_published() {
    const DiscoveryDevice device{"x", "", "", "", ""};
    const auto m = generate([](DiscoveryOut& out) { writeButtonCommand(out, 1, true); }, device);
    TEST_ASSERT_FALSE(m.ok);
}

// The largest config fits the firmware's 1024 B buffer with a long name.
void test_light_fits_with_long_name() {
    const std::string name(100, 'n');
    const DiscoveryDevice device{name, "ButtonPlus", "Model 1 Version 9", "FreeButton 3.1.0-alpha.1", "btn_a00002"};
    const auto m = generate([](DiscoveryOut& out) {
        writeLight(out, 7, 1, "wall", "buttonplus/btn_a00002/button7_wall");
    }, device);
    TEST_ASSERT_TRUE(m.ok);
    TEST_ASSERT_LESS_THAN(1024, m.payload.size());
}

void test_json_escaping() {
    char buffer[128];
    JsonOut json(buffer, sizeof(buffer));
    json.str("a", "q\"", "\\b", "\n\x01").boolean("t", true).integer("n", -42);
    const std::string_view items[] = {"x", "y\""};
    json.strArray("l", items, 2).object("o").str("k", "v").end();
    const size_t n = json.finish();
    TEST_ASSERT_EQUAL_STRING(R"({"a":"q\"\\b\n\u0001","t":true,"n":-42,"l":["x","y\""],"o":{"k":"v"}})", buffer);
    TEST_ASSERT_EQUAL(std::strlen(buffer), n);
}

void test_json_overflow() {
    char buffer[8];
    JsonOut json(buffer, sizeof(buffer));
    json.str("key", "value");
    TEST_ASSERT_EQUAL(0, json.finish());
    TEST_ASSERT_TRUE(json.overflowed());
    TEST_ASSERT_EQUAL_STRING("", buffer);
}

// Every key the firmware writes is a Home Assistant abbreviation (or a full
// key that is not also an abbreviation of something else).
void test_keys_are_known() {
    const char* written[] = {"~", "name", "def_ent_id", "uniq_id", "dev", "stat_t", "cmd_t", "avty_t", "pl_avail",
                             "pl_not_avail", "dev_cla", "evt_typ", "unit_of_meas", "schema", "ret", "rgb_stat_t",
                             "rgb_cmd_t", "rgb_cmd_tpl", "rgb_val_tpl", "bri_stat_t", "bri_cmd_t", "bri_scl",
                             "fx_stat_t", "fx_cmd_t", "fx_list", "atype", "type", "stype", "t", "pl"};
    for (const char* key : written) {
        const auto full = expandKey(ha_abbreviations::kAbbreviations, key);
        const bool isAbbreviation = full != std::string_view(key) || std::string_view(key) == "name";
        const bool isPlainFullKey = std::string_view(key) == "~" || std::string_view(key) == "schema" ||
                                    std::string_view(key) == "type";
        TEST_ASSERT_TRUE_MESSAGE(isAbbreviation || isPlainFullKey, key);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_event);
    RUN_TEST(test_button);
    RUN_TEST(test_triggers);
    RUN_TEST(test_light);
    RUN_TEST(test_sensors);
    RUN_TEST(test_remove);
    RUN_TEST(test_unnamed_device);
    RUN_TEST(test_slugged_name);
    RUN_TEST(test_overflow_not_published);
    RUN_TEST(test_cut_or_missing_parts_not_published);
    RUN_TEST(test_no_identifier_not_published);
    RUN_TEST(test_light_fits_with_long_name);
    RUN_TEST(test_json_escaping);
    RUN_TEST(test_json_overflow);
    RUN_TEST(test_keys_are_known);
    return UNITY_END();
}
