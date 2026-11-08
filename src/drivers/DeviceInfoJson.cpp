//
// DeviceInfoJson - pure JSON serialization for the DeviceInfo struct.
//
// This translation unit deliberately includes NO hardware headers (no WiFi,
// esp_*, or lvgl) and NO ArduinoJson. It takes an already-collected DeviceInfo
// and turns it into the JSON payload served by GET /api/device. Keeping it
// dependency-free lets it be unit-tested in the `native` PlatformIO environment
// (ArduinoJson transitively includes <version>, which on a case-insensitive
// macOS filesystem collides with the repo-root VERSION file). It also matches
// the hand-rolled-JSON style already used by /api/version and
// /api/bytecode/next_address.
//
// Memory fields are emitted in BYTES; the web IDE divides by 1024 to display KB.
//

#include "DeviceInfo.h"
#include "../json/JsonEscape.h"
#include <string>

namespace FreeButton {

namespace {

void appendString(std::string& out, const char* key, const std::string& value) {
    out += '"';
    out += key;
    out += "\":\"";
    escapeJsonInto(out, value);
    out += '"';
}

void appendNumber(std::string& out, const char* key, long long value) {
    out += '"';
    out += key;
    out += "\":";
    out += std::to_string(value);
}

} // namespace

std::string toJson(const DeviceInfo& info) {
    std::string out;
    // Worst case is 325 B (153 B of fixed keys/punctuation + a fully expanded
    // IPv6, a 32-char SSID and 6-digit memory figures) plus <= 106 B of
    // topology (mainConnector + 4 connectors), so 448 never reallocates.
    out.reserve(448);
    out += '{';
    appendString(out, "deviceName", info.deviceName);   out += ',';
    appendString(out, "ipv4", info.ipv4Address);         out += ',';
    appendString(out, "ipv6", info.ipv6Address);         out += ',';
    appendString(out, "ssid", info.connectedSSID);       out += ',';
    appendNumber(out, "rssi", info.signalStrength);      out += ',';
    appendString(out, "mac", info.macAddress);           out += ',';
    appendString(out, "firmware", info.firmwareVersion); out += ',';
    appendNumber(out, "uptime", info.uptime);            out += ',';
    // Memory breakdown, bytes.
    appendNumber(out, "heapFree", static_cast<long long>(info.heapFree));       out += ',';
    appendNumber(out, "heapTotal", static_cast<long long>(info.heapTotal));     out += ',';
    appendNumber(out, "heapMinFree", static_cast<long long>(info.heapMinFree)); out += ',';
    appendNumber(out, "lvglFree", static_cast<long long>(info.lvglFree));       out += ',';
    appendNumber(out, "lvglTotal", static_cast<long long>(info.lvglTotal));  out += ',';
    // Hardware topology (the web IDE lays out its virtual device from it).
    appendNumber(out, "mainConnector", info.mainConnector);                  out += ',';
    out += "\"connectors\":[";
    bool first = true;
    for (int c = 0; c < DeviceInfo::kConnectors; ++c) {
        if (info.connectorTypes[c] == 0) continue;
        if (!first) out += ',';
        first = false;
        out += "{\"id\":";
        out += static_cast<char>('0' + c);
        out += ",\"type\":";
        out += static_cast<char>('0' + (info.connectorTypes[c] & 7));
        out += '}';
    }
    out += "]}";
    return out;
}

} // namespace FreeButton
