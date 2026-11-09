//
// Native unit tests for DeviceInfo JSON serialization (toJson).
//
// Only the pure serializer is exercised here — it takes a plain DeviceInfo
// struct and produces the /api/device payload with NO hardware dependencies.
// The .cpp is included directly so no platformio.ini build_src_filter change is
// needed; its `#include "DeviceInfo.h"` resolves relative to its own directory.
//
#include <unity.h>
#include <string>

#include "../../src/drivers/DeviceInfoJson.cpp"

using FreeButton::DeviceInfo;
using FreeButton::toJson;

static DeviceInfo makeSample() {
    DeviceInfo info{};
    info.deviceName      = "btn_fab028";
    info.ipv4Address     = "192.168.1.42";
    info.ipv6Address     = "fe80::1";
    info.connectedSSID   = "HomeNetwork";
    info.signalStrength  = -55;
    info.firmwareVersion = "3.1.0-alpha.1";
    info.macAddress      = "AA:BB:CC:DD:EE:FF";
    info.uptime          = 720;
    info.heapFree        = 184320;
    info.heapTotal       = 327680;
    info.heapMinFree     = 153600;
    info.lvglFree        = 40960;
    info.lvglTotal       = 61440;
    info.jsHeapUsed      = 7200;
    info.jsHeapFree      = 8100;
    info.jsHeapSize      = 16384;
    return info;
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

void test_contains_network_fields() {
    const std::string json = toJson(makeSample());
    TEST_ASSERT_TRUE(contains(json, "\"ipv4\":\"192.168.1.42\""));
    TEST_ASSERT_TRUE(contains(json, "\"ipv6\":\"fe80::1\""));
    TEST_ASSERT_TRUE(contains(json, "\"ssid\":\"HomeNetwork\""));
    TEST_ASSERT_TRUE(contains(json, "\"mac\":\"AA:BB:CC:DD:EE:FF\""));
    TEST_ASSERT_TRUE(contains(json, "\"firmware\":\"3.1.0-alpha.1\""));
}

void test_contains_numeric_fields() {
    const std::string json = toJson(makeSample());
    // Numbers are emitted unquoted.
    TEST_ASSERT_TRUE(contains(json, "\"rssi\":-55"));
    TEST_ASSERT_TRUE(contains(json, "\"uptime\":720"));
}

void test_memory_is_bytes_not_kb() {
    const std::string json = toJson(makeSample());
    // Serializer must emit raw bytes; KB conversion is the web's job.
    TEST_ASSERT_TRUE(contains(json, "\"heapFree\":184320"));
    TEST_ASSERT_TRUE(contains(json, "\"heapTotal\":327680"));
    TEST_ASSERT_TRUE(contains(json, "\"heapMinFree\":153600"));
    TEST_ASSERT_TRUE(contains(json, "\"lvglFree\":40960"));
    TEST_ASSERT_TRUE(contains(json, "\"lvglTotal\":61440"));
    TEST_ASSERT_TRUE(contains(json, "\"jsHeapUsed\":7200"));
    TEST_ASSERT_TRUE(contains(json, "\"jsHeapFree\":8100"));
    TEST_ASSERT_TRUE(contains(json, "\"jsHeapSize\":16384"));
}

void test_valid_json_shape() {
    const std::string json = toJson(makeSample());
    TEST_ASSERT_TRUE(json.size() > 2);
    TEST_ASSERT_EQUAL_CHAR('{', json.front());
    TEST_ASSERT_EQUAL_CHAR('}', json.back());
}

void test_escapes_special_characters() {
    DeviceInfo info = makeSample();
    // An SSID with a quote and backslash must be escaped, not break the JSON.
    info.connectedSSID = "My \"Wi-Fi\" \\ Net";
    const std::string json = toJson(info);
    TEST_ASSERT_TRUE(contains(json, "\\\"Wi-Fi\\\""));
    TEST_ASSERT_TRUE(contains(json, "\\\\"));
    // The next field's key must still be intact (structure not corrupted).
    TEST_ASSERT_TRUE(contains(json, "\"rssi\":"));
}

void test_exact_key_set() {
    // Guards the C++/TS contract: every key the web's DeviceInfoData expects must
    // be present. A firmware-side rename fails this loudly (see app-state.ts).
    const std::string json = toJson(makeSample());
    for (const char* key : {
             "\"deviceName\"", "\"ipv4\"", "\"ipv6\"", "\"ssid\"", "\"rssi\"",
             "\"mac\"", "\"firmware\"", "\"uptime\"", "\"heapFree\"", "\"heapTotal\"",
             "\"heapMinFree\"", "\"lvglFree\"", "\"lvglTotal\"",
             "\"jsHeapUsed\"", "\"jsHeapFree\"", "\"jsHeapSize\"",
             "\"mainConnector\"", "\"connectors\"", "\"commit\"", "\"dirty\"", "\"model\"" }) {
        TEST_ASSERT_TRUE_MESSAGE(contains(json, key), key);
    }
}

void test_connectors_list_only_present_ones() {
    DeviceInfo info = makeSample();
    info.connectorTypes[2] = 2;   // large display
    info.connectorTypes[3] = 1;   // bar
    info.mainConnector = 2;
    const std::string json = toJson(info);
    TEST_ASSERT_TRUE(contains(json, "\"mainConnector\":2,"));
    TEST_ASSERT_TRUE(contains(json, "\"connectors\":[{\"id\":2,\"type\":2},{\"id\":3,\"type\":1}]}"));
}

void test_build_identity() {
    DeviceInfo info = makeSample();
    info.gitCommit = "30df729";
    info.dirty = true;
    info.model = "Model 1 Version 9";
    const std::string json = toJson(info);
    TEST_ASSERT_TRUE(contains(json, "\"firmware\":\"3.1.0-alpha.1\",\"commit\":\"30df729\",\"dirty\":true,\"model\":\"Model 1 Version 9\","));
    const std::string none = toJson(makeSample());
    TEST_ASSERT_TRUE(contains(none, "\"commit\":\"\",\"dirty\":false,\"model\":\"\","));
}

void test_no_connectors_is_empty_list() {
    const std::string json = toJson(makeSample());
    TEST_ASSERT_TRUE(contains(json, "\"mainConnector\":-1,\"connectors\":[]}"));
}

void test_not_connected_placeholders_pass_through() {
    DeviceInfo info = makeSample();
    info.ipv4Address = "Not connected";
    info.connectedSSID = "Not connected";
    const std::string json = toJson(info);
    TEST_ASSERT_TRUE(contains(json, "\"ipv4\":\"Not connected\""));
    TEST_ASSERT_TRUE(contains(json, "\"ssid\":\"Not connected\""));
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_contains_network_fields);
    RUN_TEST(test_connectors_list_only_present_ones);
    RUN_TEST(test_build_identity);
    RUN_TEST(test_no_connectors_is_empty_list);
    RUN_TEST(test_contains_numeric_fields);
    RUN_TEST(test_memory_is_bytes_not_kb);
    RUN_TEST(test_valid_json_shape);
    RUN_TEST(test_escapes_special_characters);
    RUN_TEST(test_exact_key_set);
    RUN_TEST(test_not_connected_placeholders_pass_through);
    return UNITY_END();
}
