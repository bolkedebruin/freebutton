//
// HaJson - minimal JSON object writer into a fixed caller buffer.
//
// No heap, no intermediate document: every call appends to the buffer right
// away. String values may be passed in parts (a, b, c, ...) that are written
// back to back, so callers never build temporaries. Keys are written as
// given (callers pass Home Assistant's abbreviated keys directly).
//
// Pure, header-only: unit-tested on the host (test/test_ha_discovery).
//

#ifndef FREEBUTTON_HAJSON_H
#define FREEBUTTON_HAJSON_H

#include <cstddef>
#include <cstdio>
#include <string_view>

namespace freebutton::ha {

class JsonOut {
public:
    // Opens the top-level object.
    JsonOut(char* buffer, const size_t capacity) : buffer_(buffer), capacity_(capacity) { put('{'); }

    // "key":"<parts...>"
    template <typename... Parts>
    JsonOut& str(const std::string_view key, const Parts&... parts) {
        name(key);
        put('"');
        (escaped(std::string_view(parts)), ...);
        put('"');
        return *this;
    }

    JsonOut& boolean(const std::string_view key, const bool value) {
        name(key);
        raw(value ? "true" : "false");
        return *this;
    }

    JsonOut& integer(const std::string_view key, const long value) {
        name(key);
        char digits[24];
        const int n = std::snprintf(digits, sizeof(digits), "%ld", value);
        raw(std::string_view(digits, n > 0 ? static_cast<size_t>(n) : 0));
        return *this;
    }

    JsonOut& strArray(const std::string_view key, const std::string_view* items, const size_t count) {
        name(key);
        put('[');
        for (size_t i = 0; i < count; ++i) {
            if (i) put(',');
            put('"');
            escaped(items[i]);
            put('"');
        }
        put(']');
        return *this;
    }

    // "key":{ ... end()
    JsonOut& object(const std::string_view key) {
        name(key);
        put('{');
        ++depth_;
        first_ = true;
        return *this;
    }

    JsonOut& end() {
        if (depth_ > 0) {
            put('}');
            --depth_;
            first_ = false;
        }
        return *this;
    }

    // Closes all open objects and NUL-terminates. Length, or 0 on overflow
    // (the buffer then holds an empty string).
    size_t finish() {
        if (!finished_) {
            while (depth_ > 0) end();
            put('}');
            finished_ = true;
        }
        if (overflow_ || capacity_ == 0) {
            if (capacity_ > 0) buffer_[0] = '\0';
            return 0;
        }
        buffer_[length_] = '\0';
        return length_;
    }

    [[nodiscard]] bool overflowed() const { return overflow_; }

private:
    void put(const char c) {
        if (length_ + 1 >= capacity_) {  // keep room for the NUL
            overflow_ = true;
            return;
        }
        buffer_[length_++] = c;
    }

    void raw(const std::string_view s) {
        for (const char c : s) put(c);
    }

    void escaped(const std::string_view s) {
        static constexpr char kHex[] = "0123456789abcdef";
        for (const char c : s) {
            const auto u = static_cast<unsigned char>(c);
            if (c == '"' || c == '\\') {
                put('\\');
                put(c);
            } else if (u < 0x20) {
                switch (c) {
                    case '\n': raw("\\n"); break;
                    case '\r': raw("\\r"); break;
                    case '\t': raw("\\t"); break;
                    default:
                        raw("\\u00");
                        put(kHex[u >> 4]);
                        put(kHex[u & 0xF]);
                }
            } else {
                put(c);  // UTF-8 passes through unchanged
            }
        }
    }

    void name(const std::string_view key) {
        if (!first_) put(',');
        first_ = false;
        put('"');
        escaped(key);
        put('"');
        put(':');
    }

    char* buffer_;
    size_t capacity_;
    size_t length_ = 0;
    unsigned depth_ = 0;
    bool first_ = true;
    bool overflow_ = false;
    bool finished_ = false;
};

} // namespace freebutton::ha

#endif //FREEBUTTON_HAJSON_H
