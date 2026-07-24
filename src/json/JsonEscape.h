#pragma once

//
// Minimal, dependency-free JSON string escaper.
//
// Shared by the hand-rolled JSON emitters that don't (and shouldn't) pull in a
// full JSON library: the /api/device serializer (src/drivers/DeviceInfoJson.cpp,
// which must stay native-testable) and the WebSocket REPL (WebSocketREPL.cpp).
//
// Includes only <string>/<string_view>, so it compiles in the PlatformIO
// `native` test environment as well as on-device.
//

#include <string>
#include <string_view>

namespace FreeButton {

// Escape a string for embedding inside a JSON string literal, APPENDING to
// `out`. Handles the mandatory escapes (" \ \n \r \t) and emits any remaining
// control character (< 0x20) as \u00XX so the result is always valid JSON.
//
// Prefer this over escapeJsonString() when you are building a larger document:
// it escapes straight into the destination instead of allocating an
// intermediate std::string per field.
inline void escapeJsonInto(std::string& out, std::string_view in) {
    // constexpr array (not a `const char*` variable): the pointer form is
    // emitted into .dram0.data on ESP32, this lives entirely in flash rodata.
    static constexpr char hex[] = "0123456789abcdef";
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
                break;
        }
    }
}

// Standalone form, for callers that need the escaped value as its own string
// (e.g. WebSocketREPL, which truncates the result before embedding it).
inline std::string escapeJsonString(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 8);
    escapeJsonInto(out, in);
    return out;
}

} // namespace FreeButton
