//
// ButtonEventPlan - see header.
//

#include "ButtonEventPlan.h"

#include <cstdio>
#include <cstring>

#include "config.h"

namespace freebutton::ha {

// --- EventTypeList / PlanProblem ---------------------------------------------

bool EventTypeList::add(const std::string_view type) {
    for (size_t i = 0; i < count_; ++i) {
        if ((*this)[i] == type) return true;
    }
    if (count_ >= kMaxTypes || type.size() > kMaxLength) {
        return false;
    }
    std::memcpy(items_[count_], type.data(), type.size());
    items_[count_][type.size()] = '\0';
    lengths_[count_] = static_cast<uint8_t>(type.size());
    ++count_;
    return true;
}

void PlanProblem::set(const std::string_view payload, const std::string_view canonicalTopic) {
    std::snprintf(text_, sizeof(text_), "configured payload '%.*s' on %.*s is not a JSON object with an event_type",
                  static_cast<int>(payload.size()), payload.data(),
                  static_cast<int>(canonicalTopic.size()), canonicalTopic.data());
}

void PlanProblem::setMessage(const std::string_view text) {
    std::snprintf(text_, sizeof(text_), "%.*s", static_cast<int>(text.size()), text.data());
}

ButtonEventPlan unannouncedPlan(const std::string_view reason) {
    ButtonEventPlan plan;  // announceEvent / mirror* false
    plan.problem.setMessage(reason);
    return plan;
}

// --- Topics -------------------------------------------------------------------

std::string canonicalButtonStateTopic(std::string_view deviceId, const int position) {
    return std::string(PUBLICMQTTID) + "/" + std::string(deviceId) + "/button_" + std::to_string(position) + "/state";
}

size_t canonicalButtonStateTopic(char* buffer, const size_t capacity, const std::string_view deviceId,
                                 const int position) {
    const int n = std::snprintf(buffer, capacity, PUBLICMQTTID "/%.*s/button_%d/state",
                                static_cast<int>(deviceId.size()), deviceId.data(), position);
    if (n < 0 || static_cast<size_t>(n) >= capacity) {
        if (capacity > 0) buffer[0] = '\0';
        return 0;
    }
    return static_cast<size_t>(n);
}

std::string buttonCommandTopic(std::string_view deviceId, const int position) {
    return std::string(PUBLICMQTTID) + "/" + std::string(deviceId) + "/button_" + std::to_string(position) + "/set";
}

std::string_view defaultPayloadFor(const EventType eventType) {
    switch (eventType) {
        case CLICK:      return CLICK_PAYLOAD;
        case LONG_PRESS: return LONG_PRESS_PAYLOAD;
        case RELEASE:    return RELEASE_PAYLOAD;
        default:         return {};
    }
}

// --- jsonEventType: a small JSON scanner, no heap, no recursion ----------------
//
// Accepts what ArduinoJson's deserializeJson() accepted before (so a config
// that worked keeps its Home Assistant event entity): "double" and 'single'
// quoted strings and keys, unquoted [0-9A-Za-z_] keys, ArduinoJson's escapes,
// literal control characters in strings, content after the top-level object
// ignored, at most 10 nesting levels. Numbers are checked loosely (a run of
// number characters), i.e. this never rejects a number ArduinoJson accepted.

namespace {

// ArduinoJson's default nesting limit is 10 including the top-level object.
constexpr int kMaxNestedContainers = 9;

class Scanner {
public:
    explicit Scanner(const std::string_view s) : s_(s) {}

    // Top level: an object. Finds its "event_type" member (last one wins, as
    // in ArduinoJson) and copies a string value into out.
    bool topObject(char* out, const size_t capacity, size_t& length, bool& found) {
        space();
        if (!eat('{')) return false;
        space();
        if (eat('}')) return true;
        for (;;) {
            space();
            char key[12];  // long enough for "event_type"
            size_t keyLength;
            bool keyFits;
            if (!readKey(key, sizeof(key), keyLength, keyFits)) return false;
            space();
            if (!eat(':')) return false;
            space();
            const bool isEventType = keyFits && std::string_view(key, keyLength) == "event_type";
            if (isEventType && isQuote(current())) {
                size_t n;
                bool fits;
                if (!string(out, capacity, n, fits)) return false;
                found = fits && n > 0;
                length = found ? n : 0;
            } else {
                if (isEventType) found = false;  // not a string
                if (!skipValue()) return false;
            }
            space();
            if (eat(',')) continue;
            if (eat('}')) return true;
            return false;
        }
    }

private:
    [[nodiscard]] bool atEnd() const { return i_ >= s_.size(); }
    [[nodiscard]] char current() const { return atEnd() ? '\0' : s_[i_]; }
    static bool isQuote(const char c) { return c == '"' || c == '\''; }
    static bool inUnquotedKey(const char c) {  // ArduinoJson's canBeInNonQuotedString
        return (c >= '0' && c <= '9') || (c >= '_' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }
    static bool inNumber(const char c) {  // ArduinoJson's canBeInNumber
        return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E';
    }

    bool eat(const char c) {
        if (current() != c) return false;
        ++i_;
        return true;
    }

    void space() {
        while (!atEnd() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    bool literal(const std::string_view word) {
        if (s_.substr(i_, word.size()) != word) return false;
        i_ += word.size();
        return true;
    }

    bool number() {
        const size_t start = i_;
        bool digit = false;
        while (!atEnd() && inNumber(s_[i_])) {
            digit = digit || (s_[i_] >= '0' && s_[i_] <= '9');
            ++i_;
        }
        return i_ > start && digit;
    }

    static bool hex(const char c, uint32_t& v) {
        if (c >= '0' && c <= '9') { v = v * 16 + (c - '0'); return true; }
        if (c >= 'a' && c <= 'f') { v = v * 16 + (c - 'a' + 10); return true; }
        if (c >= 'A' && c <= 'F') { v = v * 16 + (c - 'A' + 10); return true; }
        return false;
    }

    bool hex4(uint32_t& v) {
        v = 0;
        for (int k = 0; k < 4; ++k) {
            if (atEnd() || !hex(s_[i_++], v)) return false;
        }
        return true;
    }

    // Appends to out (when given); `fits` turns false once it overflows.
    static void append(char* out, const size_t capacity, size_t& n, bool& fits, const char c) {
        if (!out) return;
        if (n + 1 >= capacity) { fits = false; return; }
        out[n++] = c;
    }

    static void appendUtf8(char* out, const size_t capacity, size_t& n, bool& fits, const uint32_t cp) {
        if (cp < 0x80) {
            append(out, capacity, n, fits, static_cast<char>(cp));
        } else if (cp < 0x800) {
            append(out, capacity, n, fits, static_cast<char>(0xC0 | (cp >> 6)));
            append(out, capacity, n, fits, static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            append(out, capacity, n, fits, static_cast<char>(0xE0 | (cp >> 12)));
            append(out, capacity, n, fits, static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            append(out, capacity, n, fits, static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            append(out, capacity, n, fits, static_cast<char>(0xF0 | (cp >> 18)));
            append(out, capacity, n, fits, static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            append(out, capacity, n, fits, static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            append(out, capacity, n, fits, static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    // A quoted string at i_ (" or '). Unescaped into out (may be null);
    // fits=false if it did not fit. false on a syntax error.
    bool string(char* out, const size_t capacity, size_t& n, bool& fits) {
        n = 0;
        fits = true;
        const char quote = s_[i_++];
        while (!atEnd()) {
            const char c = s_[i_++];
            if (c == quote) {
                if (out && capacity > 0) out[fits ? n : 0] = '\0';
                return true;
            }
            if (c != '\\') {
                append(out, capacity, n, fits, c);
                continue;
            }
            if (atEnd()) return false;
            switch (const char e = s_[i_++]) {
                case '"': case '\'': case '\\': case '/': append(out, capacity, n, fits, e); break;
                case 'b': append(out, capacity, n, fits, '\b'); break;
                case 'f': append(out, capacity, n, fits, '\f'); break;
                case 'n': append(out, capacity, n, fits, '\n'); break;
                case 'r': append(out, capacity, n, fits, '\r'); break;
                case 't': append(out, capacity, n, fits, '\t'); break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && literal("\\u")) {
                        uint32_t low;
                        if (!hex4(low)) return false;
                        if (low >= 0xDC00 && low < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            appendUtf8(out, capacity, n, fits, cp);
                            cp = low;
                        }
                    }
                    appendUtf8(out, capacity, n, fits, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;  // unterminated
    }

    // A key: quoted, or unquoted [0-9A-Za-z_] (ArduinoJson accepts both).
    bool readKey(char* out, const size_t capacity, size_t& n, bool& fits) {
        if (isQuote(current())) {
            return string(out, capacity, n, fits);
        }
        n = 0;
        fits = true;
        const size_t start = i_;
        while (!atEnd() && inUnquotedKey(s_[i_])) {
            append(out, capacity, n, fits, s_[i_++]);
        }
        if (out && capacity > 0) out[fits ? n : 0] = '\0';
        return i_ > start;
    }

    bool skipKey() {
        size_t n;
        bool fits;
        space();
        if (!readKey(nullptr, 0, n, fits)) return false;
        space();
        return eat(':');
    }

    // Skips one value of any depth without recursion: a bit stack records
    // whether each open container is an object (1) or an array (0).
    bool skipValue() {
        uint32_t objects = 0;
        int depth = 0;
        for (;;) {
            // A value starts here.
            space();
            const char c = current();
            if (c == '{' || c == '[') {
                if (depth >= kMaxNestedContainers) return false;
                ++i_;
                const bool object = c == '{';
                objects = object ? (objects | (1u << depth)) : (objects & ~(1u << depth));
                ++depth;
                space();
                if (!eat(object ? '}' : ']')) {
                    if (object && !skipKey()) return false;
                    continue;  // its first element
                }
                --depth;  // empty container: a complete value
            } else if (isQuote(c)) {
                size_t n;
                bool fits;
                if (!string(nullptr, 0, n, fits)) return false;
            } else if (c == 't') {
                if (!literal("true")) return false;
            } else if (c == 'f') {
                if (!literal("false")) return false;
            } else if (c == 'n') {
                if (!literal("null")) return false;
            } else if (!number()) {
                return false;
            }

            // A value ended: close containers until another element follows.
            for (;;) {
                if (depth == 0) return true;
                space();
                const bool object = (objects >> (depth - 1)) & 1u;
                if (eat(',')) {
                    if (object && !skipKey()) return false;
                    break;  // next element
                }
                if (!eat(object ? '}' : ']')) return false;
                --depth;
            }
        }
    }

    std::string_view s_;
    size_t i_ = 0;
};

} // namespace

bool jsonEventType(const std::string_view payload, char* out, const size_t capacity, size_t& length) {
    length = 0;
    if (!out || capacity == 0) return false;
    out[0] = '\0';
    bool found = false;
    Scanner scanner(payload);
    if (!scanner.topObject(out, capacity, length, found) || !found) {
        out[0] = '\0';
        length = 0;
        return false;
    }
    out[length] = '\0';
    return true;
}

std::optional<std::string> jsonEventType(const std::string_view payload) {
    std::string value(payload.size() + 1, '\0');
    size_t length = 0;
    if (!jsonEventType(payload, value.data(), value.size(), length)) {
        return std::nullopt;
    }
    value.resize(length);
    return value;
}

// --- The plan -------------------------------------------------------------------

ButtonEventPlan planButtonEvents(std::span<const PressTopic> topics,
                                 const uint8_t haBrokerId,
                                 const std::string_view canonicalTopic) {
    ButtonEventPlan plan;
    plan.announceEvent = true;

    // Button::publishToTopic publishes only the FIRST topic configured for an
    // event type, so only that one is relevant.
    auto firstFor = [&topics](const EventType eventType) -> const PressTopic* {
        for (const PressTopic& t : topics) {
            if (t.eventType == eventType) return &t;
        }
        return nullptr;
    };

    auto onCanonical = [&](const PressTopic& t) {
        return t.brokerId == haBrokerId && t.topic == canonicalTopic;
    };

    auto effectivePayload = [](const PressTopic& t) {
        return t.payload.empty() ? defaultPayloadFor(t.eventType) : t.payload;
    };

    // Click / long press always reach the canonical topic: either the user's
    // config already publishes the exact default payload there, or we mirror.
    const PressTopic* click = firstFor(CLICK);
    const PressTopic* longPress = firstFor(LONG_PRESS);
    plan.mirrorClick = !(click && onCanonical(*click) && effectivePayload(*click) == CLICK_PAYLOAD);
    plan.mirrorLongPress = !(longPress && onCanonical(*longPress) && effectivePayload(*longPress) == LONG_PRESS_PAYLOAD);
    plan.eventTypes.add("click");
    plan.eventTypes.add("longpress");

    // Anything the user's own config puts on the canonical topic must be a
    // JSON event too, and its event_type must be listed. An event_type longer
    // than EventTypeList::kMaxLength is treated as unusable.
    for (const EventType eventType : {CLICK, LONG_PRESS, RELEASE}) {
        const PressTopic* t = firstFor(eventType);
        if (!t || !onCanonical(*t)) {
            continue;
        }
        const auto payload = effectivePayload(*t);
        char type[EventTypeList::kMaxLength + 1];
        size_t length = 0;
        if (jsonEventType(payload, type, sizeof(type), length) &&
            plan.eventTypes.add(std::string_view(type, length))) {
            continue;
        }
        plan.announceEvent = false;
        plan.problem.set(payload, canonicalTopic);
    }

    if (!plan.announceEvent) {
        plan.mirrorClick = false;
        plan.mirrorLongPress = false;
    }
    return plan;
}

} // namespace freebutton::ha
