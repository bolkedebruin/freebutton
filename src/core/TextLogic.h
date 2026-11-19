//
// TextLogic - small text helpers the pure parts of the HTTP server share
// (S3Logic.h, AuthLogic.h, StreamLogic.h): suffix test, hex and percent
// encoding, calendar fields of a Unix time, the HTTP date and the content
// type of a file name.
//
// No Arduino / ESP headers and no heap: every helper writes into a caller
// buffer or into a sink with a Put(char) member. Covered by the native tests
// of the headers that use it (test/test_auth, test/test_s3_logic,
// test/test_stream_logic).
//

#ifndef FREEBUTTON_TEXTLOGIC_H
#define FREEBUTTON_TEXTLOGIC_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fbtext {

inline bool endsWith(const char* s, size_t len, const char* suffix) {
    const size_t n = strlen(suffix);
    return len >= n && memcmp(s + len - n, suffix, n) == 0;
}

// ---------------------------------------------------------------------------
// Hex and percent encoding
// ---------------------------------------------------------------------------

inline constexpr char kHexLower[] = "0123456789abcdef";
inline constexpr char kHexUpper[] = "0123456789ABCDEF";

// Value of one hex digit (either case), or -1.
inline int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// n bytes -> 2*n lower-case hex digits in out (no NUL).
inline void toHex(const uint8_t* in, size_t n, char* out) {
    for (size_t i = 0; i < n; ++i) {
        out[2 * i] = kHexLower[in[i] >> 4];
        out[2 * i + 1] = kHexLower[in[i] & 15];
    }
}

// RFC 3986 unreserved characters.
inline bool isUnreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

// Percent-encodes everything but the unreserved characters (upper-case hex).
// With keepSlash, '/' is kept too (a path); otherwise it becomes %2F.
template <class Sink>
void percentEncode(Sink& out, const char* s, size_t len, bool keepSlash) {
    for (size_t i = 0; i < len; ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (isUnreserved(c) || (keepSlash && c == '/')) {
            out.Put(s[i]);
        } else {
            out.Put('%');
            out.Put(kHexUpper[c >> 4]);
            out.Put(kHexUpper[c & 15]);
        }
    }
}

// ---------------------------------------------------------------------------
// Dates
// ---------------------------------------------------------------------------

// UTC calendar fields of a Unix time.
struct Civil {
    unsigned year, month, day, hour, minute, second, weekday;  // weekday 0 = Sunday
};

// Howard Hinnant's civil_from_days; no gmtime/TZ dependency. 32-bit math (no
// 64-bit division helpers on the ESP32): a time before 1970 gives 1970, one
// after 2106-02-07 gives that date.
inline Civil toCivil(int64_t t64) {
    const uint32_t t = t64 < 0 ? 0 : (t64 > 0xFFFFFFFFll ? 0xFFFFFFFFu : static_cast<uint32_t>(t64));
    const uint32_t days = t / 86400;
    const uint32_t secs = t % 86400;
    const uint32_t z = days + 719468;
    const uint32_t era = z / 146097;
    const uint32_t doe = z - era * 146097;
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    Civil c{};
    c.day = doy - (153 * mp + 2) / 5 + 1;
    c.month = mp < 10 ? mp + 3 : mp - 9;
    c.year = yoe + era * 400 + (c.month <= 2 ? 1 : 0);
    c.hour = secs / 3600;
    c.minute = secs / 60 % 60;
    c.second = secs % 60;
    c.weekday = (days + 4) % 7;  // 1970-01-01 was a Thursday
    return c;
}

// The last len decimal digits of val, zero-padded, into out (no NUL).
inline void putDigits(char* out, unsigned val, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        out[len - 1 - i] = static_cast<char>('0' + val % 10);
        val /= 10;
    }
}

// RFC 7231 IMF-fixdate "Sun, 08 Nov 2026 12:34:56 GMT" (29 chars + NUL): the
// HTTP Date / Last-Modified value (no strftime: it would pull ~12 KB of
// newlib printf code).
inline void formatHttpDate(int64_t t, char out[30]) {
    static constexpr char kDays[] = "SunMonTueWedThuFriSat";
    static constexpr char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const Civil c = toCivil(t);
    memcpy(out, kDays + 3 * c.weekday, 3);
    out[3] = ',';
    out[4] = ' ';
    putDigits(out + 5, c.day, 2);
    out[7] = ' ';
    memcpy(out + 8, kMonths + 3 * (c.month - 1), 3);
    out[11] = ' ';
    putDigits(out + 12, c.year, 4);
    out[16] = ' ';
    putDigits(out + 17, c.hour, 2);
    out[19] = ':';
    putDigits(out + 20, c.minute, 2);
    out[22] = ':';
    putDigits(out + 23, c.second, 2);
    memcpy(out + 25, " GMT", 5);
}

// ---------------------------------------------------------------------------
// Content types
// ---------------------------------------------------------------------------

// Content type from the extension of a file name (lower case only). A ".gz"
// name has no type here: the static files strip it first (the file is sent
// with Content-Encoding: gzip), S3 serves the gzip itself.
inline const char* contentTypeByExtension(const char* name, size_t len) {
    struct Entry {
        const char* ext;
        const char* type;
    };
    // ".woff2" needs no place before ".woff": a suffix test tells them apart.
    static constexpr Entry kTypes[] = {
        {".js", "application/javascript"},
        {".mjs", "application/javascript"},
        {".json", "application/json"},
        {".map", "application/json"},
        {".css", "text/css"},
        {".html", "text/html"},
        {".htm", "text/html"},
        {".txt", "text/plain"},
        {".wasm", "application/wasm"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".ico", "image/x-icon"},
        {".woff2", "font/woff2"},
        {".woff", "font/woff"},
        {".ttf", "font/ttf"},
        {".otf", "font/otf"},
    };
    for (const Entry& e : kTypes) {
        if (endsWith(name, len, e.ext)) return e.type;
    }
    return "application/octet-stream";
}

}  // namespace fbtext

#endif  // FREEBUTTON_TEXTLOGIC_H
