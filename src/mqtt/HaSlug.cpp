//
// HaSlug - see header.
//

#include "HaSlug.h"

#include <cstdint>

namespace {

// Transliteration of U+00C0..U+00FF (lowercased). nullptr = separator.
constexpr const char* kLatin1[64] = {
    // U+00C0..U+00CF: À Á Â Ã Ä Å Æ Ç È É Ê Ë Ì Í Î Ï
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    // U+00D0..U+00DF: Ð Ñ Ò Ó Ô Õ Ö × Ø Ù Ú Û Ü Ý Þ ß
    "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "ss",
    // U+00E0..U+00EF: à á â ã ä å æ ç è é ê ë ì í î ï
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    // U+00F0..U+00FF: ð ñ ò ó ô õ ö ÷ ø ù ú û ü ý þ ÿ
    "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y",
};

} // namespace

size_t haSlugify(const std::string_view utf8, char* out, const size_t capacity) {
    if (capacity == 0) {
        return 0;
    }
    size_t n = 0;
    bool pendingSep = false;
    bool full = false;

    auto put = [&](const char c) {
        if (n + 1 >= capacity) {
            full = true;
            return;
        }
        out[n++] = c;
    };
    auto emit = [&](const char* text) {
        if (pendingSep && n > 0) {
            put('_');
        }
        pendingSep = false;
        for (; *text; ++text) put(*text);
    };

    size_t i = 0;
    while (i < utf8.size() && !full) {
        const auto c = static_cast<uint8_t>(utf8[i]);

        if (c < 0x80) {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                const char s[2] = {static_cast<char>(c), '\0'};
                emit(s);
            } else if (c >= 'A' && c <= 'Z') {
                const char s[2] = {static_cast<char>(c - 'A' + 'a'), '\0'};
                emit(s);
            } else {
                pendingSep = true;
            }
            ++i;
            continue;
        }

        // Multi-byte UTF-8 sequence: determine its length.
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;

        const char* translit = nullptr;
        if (len == 2 && i + 1 < utf8.size()) {
            const auto c2 = static_cast<uint8_t>(utf8[i + 1]);
            const uint32_t cp = ((c & 0x1Fu) << 6) | (c2 & 0x3Fu);
            if (cp >= 0xC0 && cp <= 0xFF) {
                translit = kLatin1[cp - 0xC0];
            } else if (cp == 0x132 || cp == 0x133) {  // Ĳ ĳ
                translit = "ij";
            }
        }

        if (translit) {
            emit(translit);
        } else {
            pendingSep = true;
        }

        // Skip the lead byte and any continuation bytes.
        ++i;
        while (i < utf8.size() && --len > 0 && (static_cast<uint8_t>(utf8[i]) & 0xC0) == 0x80) {
            ++i;
        }
    }
    // A cut inside a separator run must not leave a trailing '_'.
    while (full && n > 0 && out[n - 1] == '_') {
        --n;
    }
    out[n] = '\0';
    return n;
}

std::string haSlugify(const std::string_view utf8) {
    // The slug is never longer than its input: every output byte (or "_")
    // consumes at least one input byte.
    std::string out(utf8.size() + 1, '\0');
    out.resize(haSlugify(utf8, out.data(), out.size()));
    return out;
}
