//
// AuthLogic - the pure parts of the device's request authentication:
// AWS Signature Version 4 (AWS4-HMAC-SHA256) verification of the header form
// (Authorization: AWS4-HMAC-SHA256 ...) and the presigned query form
// (X-Amz-Algorithm=...), the aws-chunked chunk signatures, and the device
// credential format.
//
// No Arduino / ESP headers and no heap. The canonical request is never built
// in memory: it is streamed straight into SHA-256 (the device passes a
// mbedTLS/hardware hasher, the native tests a portable one or a string sink).
// Unit-tested natively in test/test_auth against the AWS SigV4 test suite.
//
// Template parameters:
//   Sha  - void init(); void update(const uint8_t*, size_t); void finish(uint8_t[32]);
//   Req  - the request as the web server parsed it (DECODED path and query):
//            std::string_view method() const;           "GET", "POST", ...
//            std::string_view path() const;             decoded, e.g. "/a b"
//            size_t queryCount() const;                 URL query only (no form fields)
//            std::string_view queryName(size_t) const;  decoded
//            std::string_view queryValue(size_t) const; decoded
//            size_t headerCount() const;
//            std::string_view headerName(size_t) const; any case
//            std::string_view headerValue(size_t) const;
//            size_t contentLength() const;
//   Sink - void Put(char);
//
// Canonical URI: S3 rules (the path URI-encoded once per byte, '/' kept,
// RFC 3986 unreserved kept; no double encoding, no "." / ".." removal).
// The server hands us the decoded path, so a path is re-encoded here; that
// matches what any SigV4 client signs as long as it percent-encodes every
// reserved character on the wire. Caveat: ESPAsyncWebServer decodes a RAW '+'
// in the path or query as a space (form semantics), so a client must send '+'
// as %2B (all SigV4 clients do; a raw '+' fails with SignatureDoesNotMatch).
// Likewise an encoded "%2F" inside a segment arrives as '/' and is signed as
// '/', so keys must not contain an encoded slash.
//

#ifndef FREEBUTTON_AUTHLOGIC_H
#define FREEBUTTON_AUTHLOGIC_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace fbauth {

using sv = std::string_view;

inline constexpr sv kAlgorithm = "AWS4-HMAC-SHA256";
inline constexpr sv kRegion = "us-east-1";
inline constexpr sv kService = "s3";
inline constexpr sv kTerminator = "aws4_request";
inline constexpr sv kUnsignedPayload = "UNSIGNED-PAYLOAD";
inline constexpr sv kStreamingPayload = "STREAMING-AWS4-HMAC-SHA256-PAYLOAD";
inline constexpr sv kStreamingUnsignedTrailer = "STREAMING-UNSIGNED-PAYLOAD-TRAILER";
inline constexpr sv kEmptySha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
inline constexpr long kMaxSkewSeconds = 15 * 60;
inline constexpr long kMaxPresignExpires = 604800;  // 7 days, as S3

// Credentials: access key id "FB" + 18 base32 chars, secret 40 alphanumerics.
inline constexpr size_t kAccessKeyIdLen = 20;
inline constexpr size_t kSecretLen = 40;
inline constexpr sv kQrPrefix = "FBCRED:";
inline constexpr size_t kQrPayloadLen = 7 + kAccessKeyIdLen + 1 + kSecretLen;  // 68

enum class Status : uint8_t {
    Ok,
    MissingAuthentication,  // neither an Authorization header nor X-Amz-Algorithm
    AccessDenied,           // malformed / wrong key id / scope / expired / payload mode
    SignatureDoesNotMatch,  // well-formed, but the signature (or body hash) is wrong
    RequestTimeTooSkewed,   // valid signature, x-amz-date outside +-15 min
};

inline const char* statusCode(Status s) {
    switch (s) {
        case Status::Ok: return "Ok";
        case Status::MissingAuthentication: return "MissingAuthentication";
        case Status::AccessDenied: return "AccessDenied";
        case Status::SignatureDoesNotMatch: return "SignatureDoesNotMatch";
        case Status::RequestTimeTooSkewed: return "RequestTimeTooSkewed";
    }
    return "AccessDenied";
}

// What x-amz-content-sha256 said about the body.
enum class Payload : uint8_t {
    Unsigned,   // UNSIGNED-PAYLOAD (always for presigned requests)
    Sha256,     // hex SHA-256 of the body, in Result::payloadSha256: CHECK IT
    Streaming,  // STREAMING-AWS4-HMAC-SHA256-PAYLOAD: aws-chunked, chunk signatures
    // STREAMING-UNSIGNED-PAYLOAD-TRAILER: aws-chunked, body not signed (like
    // UNSIGNED-PAYLOAD), checksum in a trailer. aws-cli's CRT transfer client
    // (`aws s3 cp`) sends it even over plain HTTP. Only the S3 PutObject path
    // accepts it; everything else treats it as an unsupported payload.
    UnsignedStreaming,
};

// Outcome of verify(). Small and fixed (no heap); lives on the caller's stack
// or in a per-request struct.
struct Result {
    Status status = Status::MissingAuthentication;
    bool presigned = false;
    Payload payload = Payload::Unsigned;
    uint8_t payloadSha256[32] = {};  // claimed body hash when payload == Sha256
    uint8_t signature[32] = {};      // the request signature (aws-chunked seed)
    char amzDate[17] = {};           // yyyymmddThhmmssZ ("" if not parsed)
    const char* message = "";        // static, human readable
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

inline bool isUnreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

inline char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

inline bool equalsIgnoreCase(sv a, sv b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}

inline void toHex(const uint8_t* in, size_t n, char* out) {
    static constexpr char kDigits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[2 * i] = kDigits[in[i] >> 4];
        out[2 * i + 1] = kDigits[in[i] & 15];
    }
}

inline int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Exactly 2*n hex digits (either case) into n bytes.
inline bool fromHex(sv hex, uint8_t* out, size_t n) {
    if (hex.size() != 2 * n) return false;
    for (size_t i = 0; i < n; ++i) {
        const int hi = hexNibble(hex[2 * i]);
        const int lo = hexNibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

// Constant time: the comparison does not stop at the first differing byte.
inline bool digestEqual(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

// "yyyymmddThhmmssZ" -> seconds since the Unix epoch (UTC). Strict.
inline bool parseAmzDate(sv s, int64_t& epoch) {
    if (s.size() != 16 || s[8] != 'T' || s[15] != 'Z') return false;
    auto num = [&s](size_t pos, size_t len, int& v) {
        v = 0;
        for (size_t i = pos; i < pos + len; ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
            v = v * 10 + (s[i] - '0');
        }
        return true;
    };
    int y, mo, d, h, mi, se;
    if (!num(0, 4, y) || !num(4, 2, mo) || !num(6, 2, d) || !num(9, 2, h) || !num(11, 2, mi) ||
        !num(13, 2, se)) {
        return false;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return false;
    // days_from_civil (H. Hinnant)
    const int yy = y - (mo <= 2 ? 1 : 0);
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(yy - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
    epoch = days * 86400 + h * 3600 + mi * 60 + se;
    return true;
}

// Seconds since the epoch (>= 0, i.e. 1970 or later) -> UTC calendar fields.
struct CivilTime {
    unsigned year, month, day, hour, minute, second, weekday;  // weekday 0 = Sunday
};

inline CivilTime civilFromEpoch(int64_t epoch) {
    if (epoch < 0) epoch = 0;
    const uint32_t days = static_cast<uint32_t>(epoch / 86400);
    const uint32_t rem = static_cast<uint32_t>(epoch - static_cast<int64_t>(days) * 86400);
    // civil_from_days (H. Hinnant), era >= 0 here
    const uint32_t z = days + 719468;
    const uint32_t era = z / 146097;
    const uint32_t doe = z - era * 146097;
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    CivilTime t;
    t.day = doy - (153 * mp + 2) / 5 + 1;
    t.month = mp < 10 ? mp + 3 : mp - 9;
    t.year = yoe + era * 400 + (t.month <= 2 ? 1 : 0);
    t.hour = rem / 3600;
    t.minute = (rem / 60) % 60;
    t.second = rem % 60;
    t.weekday = (days + 4) % 7;  // 1970-01-01 was a Thursday
    return t;
}

inline void putDigits(char* out, unsigned val, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        out[len - 1 - i] = static_cast<char>('0' + val % 10);
        val /= 10;
    }
}

// -> "yyyymmddThhmmssZ" (17 bytes incl. NUL).
inline void formatAmzDate(int64_t epoch, char out[17]) {
    const CivilTime t = civilFromEpoch(epoch);
    putDigits(out, t.year, 4);
    putDigits(out + 4, t.month, 2);
    putDigits(out + 6, t.day, 2);
    out[8] = 'T';
    putDigits(out + 9, t.hour, 2);
    putDigits(out + 11, t.minute, 2);
    putDigits(out + 13, t.second, 2);
    out[15] = 'Z';
    out[16] = '\0';
}

// -> RFC 1123 "Sun, 08 Nov 2026 12:00:00 GMT" (30 bytes incl. NUL), the HTTP
// Date header (no strftime: it would pull ~12 KB of newlib printf code).
inline void formatHttpDate(int64_t epoch, char out[30]) {
    static constexpr char kDays[] = "SunMonTueWedThuFriSat";
    static constexpr char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const CivilTime t = civilFromEpoch(epoch);
    memcpy(out, kDays + 3 * t.weekday, 3);
    out[3] = ',';
    out[4] = ' ';
    putDigits(out + 5, t.day, 2);
    out[7] = ' ';
    memcpy(out + 8, kMonths + 3 * (t.month - 1), 3);
    out[11] = ' ';
    putDigits(out + 12, t.year, 4);
    out[16] = ' ';
    putDigits(out + 17, t.hour, 2);
    out[19] = ':';
    putDigits(out + 20, t.minute, 2);
    out[22] = ':';
    putDigits(out + 23, t.second, 2);
    memcpy(out + 25, " GMT", 5);
}

// ---------------------------------------------------------------------------
// Hashing: sinks and HMAC over a caller-supplied SHA-256
// ---------------------------------------------------------------------------

template <class Sha>
struct ShaSink {
    Sha& sha;
    void Put(char c) {
        const auto b = static_cast<uint8_t>(c);
        sha.update(&b, 1);
    }
    void write(sv s) { sha.update(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
};

// Generic sink helpers: a sink only needs Put(char); write() is used when present.
template <class Sink>
inline void putStr(Sink& out, sv s) {
    if constexpr (requires { out.write(s); }) {
        out.write(s);
    } else {
        for (char c : s) out.Put(c);
    }
}

// HMAC-SHA256 with a key of at most 64 bytes (all keys here are 32 or 44).
template <class Sha>
class Hmac {
public:
    // Works on the caller's hasher (no second SHA context on the stack).
    explicit Hmac(Sha& sha) : sha_(sha) {}

    // Keys of at most 64 bytes (all keys here are 32 or 44).
    void begin(const uint8_t* key, size_t keyLen) {
        for (size_t i = 0; i < 64; ++i) {
            pad_[i] = static_cast<uint8_t>((i < keyLen ? key[i] : 0) ^ 0x36);
        }
        sha_.init();
        sha_.update(pad_, 64);
        for (auto& b : pad_) b ^= 0x36 ^ 0x5c;  // ipad -> opad in place
    }
    void update(const uint8_t* d, size_t n) { sha_.update(d, n); }
    void update(sv s) { update(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
    void Put(char c) {
        const auto b = static_cast<uint8_t>(c);
        sha_.update(&b, 1);
    }
    void write(sv s) { update(s); }
    void finish(uint8_t out[32]) {
        uint8_t inner[32];
        sha_.finish(inner);
        sha_.init();
        sha_.update(pad_, 64);
        sha_.update(inner, 32);
        sha_.finish(out);
    }

private:
    Sha& sha_;
    uint8_t pad_[64];
};

template <class Sha>
inline void hmac(const uint8_t* key, size_t keyLen, sv msg, uint8_t out[32]) {
    Sha sha;
    Hmac<Sha> h(sha);
    h.begin(key, keyLen);
    h.update(msg);
    h.finish(out);
}

template <class Sha>
inline void sha256(const uint8_t* d, size_t n, uint8_t out[32]) {
    Sha sha;
    sha.init();
    sha.update(d, n);
    sha.finish(out);
}

// kSigning = HMAC(HMAC(HMAC(HMAC("AWS4" + secret, date8), region), service), "aws4_request")
template <class Sha>
inline bool deriveSigningKey(sv secret, sv date8, sv region, sv service, uint8_t out[32]) {
    if (secret.size() > 60 || date8.size() != 8) return false;
    Sha sha;
    Hmac<Sha> h(sha);
    uint8_t k[64];
    memcpy(k, "AWS4", 4);
    memcpy(k + 4, secret.data(), secret.size());
    h.begin(k, 4 + secret.size());
    memset(k, 0, sizeof k);
    h.update(date8);
    h.finish(out);
    const sv next[3] = {region, service, kTerminator};
    for (const sv& msg : next) {
        memcpy(k, out, 32);
        h.begin(k, 32);
        h.update(msg);
        h.finish(out);
    }
    memset(k, 0, sizeof k);
    return true;
}

// ---------------------------------------------------------------------------
// Canonical request pieces
// ---------------------------------------------------------------------------

// URI-encode one string (RFC 3986 unreserved kept, uppercase hex). With
// keepSlash, '/' is kept (canonical URI); otherwise it is %2F (query).
template <class Sink>
inline void uriEncode(Sink& out, sv s, bool keepSlash) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (isUnreserved(c) || (keepSlash && c == '/')) {
            out.Put(ch);
        } else {
            out.Put('%');
            out.Put(kDigits[c >> 4]);
            out.Put(kDigits[c & 15]);
        }
    }
}

// Lazily URI-encodes a string one output char at a time (for comparisons
// of encoded forms without a buffer). next() returns -1 at the end.
class EncodedChars {
public:
    explicit EncodedChars(sv s) : s_(s) {}
    int next() {
        static constexpr char kDigits[] = "0123456789ABCDEF";
        if (np_ > 0) return static_cast<unsigned char>(pending_[2 - np_--]);
        if (i_ >= s_.size()) return -1;
        const auto c = static_cast<unsigned char>(s_[i_++]);
        if (isUnreserved(c)) return c;
        pending_[0] = kDigits[c >> 4];
        pending_[1] = kDigits[c & 15];
        np_ = 2;
        return '%';
    }

private:
    sv s_;
    size_t i_ = 0;
    char pending_[2] = {};
    uint8_t np_ = 0;
};

// Byte-order comparison of uriEncode(a) and uriEncode(b).
inline int compareEncoded(sv a, sv b) {
    EncodedChars x(a), y(b);
    for (;;) {
        const int ca = x.next();
        const int cb = y.next();
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca < 0) return 0;
    }
}

// Trimmed, inner whitespace runs collapsed to one space.
template <class Sink>
inline void putTrimmedHeaderValue(Sink& out, sv v) {
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t b = 0, e = v.size();
    while (b < e && ws(v[b])) ++b;
    while (e > b && ws(v[e - 1])) --e;
    bool space = false;
    for (size_t i = b; i < e; ++i) {
        if (ws(v[i])) {
            space = true;
            continue;
        }
        if (space) out.Put(' ');
        space = false;
        out.Put(v[i]);
    }
}

// First header with this name (case-insensitive).
template <class Req>
inline bool findHeader(const Req& req, sv name, sv& value) {
    for (size_t i = 0; i < req.headerCount(); ++i) {
        if (equalsIgnoreCase(req.headerName(i), name)) {
            value = req.headerValue(i);
            return true;
        }
    }
    return false;
}

template <class Req>
inline bool findQuery(const Req& req, sv name, sv& value) {
    for (size_t i = 0; i < req.queryCount(); ++i) {
        if (req.queryName(i) == name) {
            value = req.queryValue(i);
            return true;
        }
    }
    return false;
}

// Canonical query string: every URL query parameter (minus X-Amz-Signature
// for a presigned request), name and value URI-encoded, sorted by encoded
// name then encoded value, "name=value" joined with '&'. Sorted by repeated
// minimum search (O(n^2), n is tiny) so no index array is needed.
template <class Req, class Sink>
inline void writeCanonicalQuery(const Req& req, bool presigned, Sink& out) {
    const size_t n = req.queryCount();
    auto skip = [&](size_t i) { return presigned && req.queryName(i) == "X-Amz-Signature"; };
    // a < b in (encoded name, encoded value, index) order
    auto less = [&](size_t a, size_t b) {
        int c = compareEncoded(req.queryName(a), req.queryName(b));
        if (c == 0) c = compareEncoded(req.queryValue(a), req.queryValue(b));
        return c < 0 || (c == 0 && a < b);
    };
    constexpr size_t kNone = static_cast<size_t>(-1);
    size_t prev = kNone;
    bool first = true;
    for (;;) {
        size_t best = kNone;
        for (size_t i = 0; i < n; ++i) {
            if (skip(i)) continue;
            if (prev != kNone && !less(prev, i)) continue;
            if (best == kNone || less(i, best)) best = i;
        }
        if (best == kNone) break;
        if (!first) out.Put('&');
        first = false;
        uriEncode(out, req.queryName(best), false);
        out.Put('=');
        uriEncode(out, req.queryValue(best), false);
        prev = best;
    }
}

// The whole canonical request. Returns false if a signed header is missing
// from the request (or the list is malformed).
template <class Req, class Sink>
inline bool writeCanonicalRequest(const Req& req, sv signedHeaders, sv payloadHash, bool presigned,
                                  Sink& out) {
    putStr(out, req.method());
    out.Put('\n');
    const sv path = req.path();
    if (path.empty()) {
        out.Put('/');
    } else {
        uriEncode(out, path, true);
    }
    out.Put('\n');
    writeCanonicalQuery(req, presigned, out);
    out.Put('\n');
    // canonical headers, in SignedHeaders order
    size_t pos = 0;
    while (pos <= signedHeaders.size()) {
        size_t end = signedHeaders.find(';', pos);
        if (end == sv::npos) end = signedHeaders.size();
        const sv name = signedHeaders.substr(pos, end - pos);
        if (name.empty()) return false;
        putStr(out, name);
        out.Put(':');
        bool found = false;
        for (size_t i = 0; i < req.headerCount(); ++i) {
            if (!equalsIgnoreCase(req.headerName(i), name)) continue;
            if (found) out.Put(',');
            found = true;
            putTrimmedHeaderValue(out, req.headerValue(i));
        }
        if (!found) return false;
        out.Put('\n');
        pos = end + 1;
    }
    out.Put('\n');
    putStr(out, signedHeaders);
    out.Put('\n');
    putStr(out, payloadHash);
    return true;
}

// "<date8>/<region>/<service>/aws4_request"
template <class Sink>
inline void writeScope(Sink& out, sv date8, sv region, sv service) {
    putStr(out, date8);
    out.Put('/');
    putStr(out, region);
    out.Put('/');
    putStr(out, service);
    out.Put('/');
    putStr(out, kTerminator);
}

// "AWS4-HMAC-SHA256\n<amzDate>\n<scope>\n<hex(sha256(canonical request))>"
template <class Sink>
inline void writeStringToSign(Sink& out, sv amzDate, sv region, sv service, const uint8_t creqHash[32]) {
    putStr(out, kAlgorithm);
    out.Put('\n');
    putStr(out, amzDate);
    out.Put('\n');
    writeScope(out, amzDate.substr(0, 8), region, service);
    out.Put('\n');
    char hex[64];
    toHex(creqHash, 32, hex);
    putStr(out, sv(hex, 64));
}

// aws-chunked: the signature of one chunk. String to sign:
// "AWS4-HMAC-SHA256-PAYLOAD\n<amzDate>\n<scope>\n<previous signature>\n
//  <sha256("")>\n<sha256(chunk data)>". The first chunk's previous
// signature is the request (seed) signature; the final chunk is empty.
template <class Sha>
inline void chunkSignature(const uint8_t signingKey[32], sv amzDate, sv region, sv service,
                           const uint8_t prevSignature[32], const uint8_t chunkSha256[32],
                           uint8_t out[32]) {
    Sha sha;
    Hmac<Sha> h(sha);
    h.begin(signingKey, 32);
    h.update("AWS4-HMAC-SHA256-PAYLOAD\n");
    h.update(amzDate);
    h.Put('\n');
    writeScope(h, amzDate.substr(0, 8), region, service);
    h.Put('\n');
    char hex[64];
    toHex(prevSignature, 32, hex);
    h.update(sv(hex, 64));
    h.Put('\n');
    h.update(kEmptySha256);
    h.Put('\n');
    toHex(chunkSha256, 32, hex);
    h.update(sv(hex, 64));
    h.finish(out);
}

// ---------------------------------------------------------------------------
// Parsing the authentication parameters
// ---------------------------------------------------------------------------

struct AuthFields {
    bool presigned = false;
    sv accessKeyId, date8, region, service, terminator;
    sv signedHeaders;
    sv signatureHex;
    sv amzDate;
    sv payloadHash;     // as it goes into the canonical request
    int64_t expires = 0;  // presigned only
};

// "<akid>/<date8>/<region>/<service>/aws4_request"
inline bool splitCredential(sv cred, AuthFields& f) {
    sv parts[5];
    size_t pos = 0;
    for (int i = 0; i < 5; ++i) {
        size_t end = cred.find('/', pos);
        if (i == 4) {
            if (end != sv::npos) return false;
            end = cred.size();
        } else if (end == sv::npos) {
            return false;
        }
        parts[i] = cred.substr(pos, end - pos);
        pos = end + 1;
    }
    f.accessKeyId = parts[0];
    f.date8 = parts[1];
    f.region = parts[2];
    f.service = parts[3];
    f.terminator = parts[4];
    return true;
}

// "AWS4-HMAC-SHA256 Credential=..., SignedHeaders=..., Signature=..."
inline bool parseAuthorization(sv v, AuthFields& f, const char*& msg) {
    if (v.substr(0, kAlgorithm.size()) != kAlgorithm || v.size() <= kAlgorithm.size() ||
        v[kAlgorithm.size()] != ' ') {
        msg = "unsupported authorization type (only AWS4-HMAC-SHA256)";
        return false;
    }
    v.remove_prefix(kAlgorithm.size());
    bool haveCred = false;
    while (!v.empty()) {
        while (!v.empty() && (v.front() == ' ' || v.front() == ',')) v.remove_prefix(1);
        if (v.empty()) break;
        size_t end = v.find(',');
        if (end == sv::npos) end = v.size();
        sv item = v.substr(0, end);
        while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
        v.remove_prefix(end);
        const size_t eq = item.find('=');
        if (eq == sv::npos) {
            msg = "malformed Authorization header";
            return false;
        }
        const sv key = item.substr(0, eq);
        const sv val = item.substr(eq + 1);
        if (key == "Credential") {
            if (!splitCredential(val, f)) {
                msg = "malformed Credential";
                return false;
            }
            haveCred = true;
        } else if (key == "SignedHeaders") {
            f.signedHeaders = val;
        } else if (key == "Signature") {
            f.signatureHex = val;
        }
    }
    if (!haveCred || f.signedHeaders.empty() || f.signatureHex.empty()) {
        msg = "Authorization needs Credential, SignedHeaders and Signature";
        return false;
    }
    return true;
}

inline bool parseUnsigned(sv s, int64_t& v) {
    if (s.empty() || s.size() > 10) return false;
    v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    return true;
}

inline bool hasToken(sv list, sv token) {
    size_t pos = 0;
    while (pos <= list.size()) {
        size_t end = list.find(';', pos);
        if (end == sv::npos) end = list.size();
        if (list.substr(pos, end - pos) == token) return true;
        pos = end + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

struct VerifyConfig {
    sv accessKeyId;
    sv region = kRegion;
    sv service = kService;
    // Current time (Unix seconds); 0 = unknown (clock not synced): the time
    // window is not checked, the signature still is.
    int64_t now = 0;
    // Payload line of a presigned request. S3 uses UNSIGNED-PAYLOAD; the
    // generic SigV4 test suite signs the empty-body hash.
    sv presignedPayload = kUnsignedPayload;
};

// Verifies the request's signature. keyFor(date8, uint8_t out[32]) -> bool
// supplies the signing key for a date (the device caches it per date).
// Order: parse (AccessDenied / MissingAuthentication), signature
// (SignatureDoesNotMatch), then the time window (RequestTimeTooSkewed /
// AccessDenied "expired"), so a clock answer is only given to a correctly
// signed request.
template <class Sha, class Req, class KeyFn>
inline Status verify(const Req& req, const VerifyConfig& cfg, KeyFn&& keyFor, Result& r) {
    r = Result{};
    auto fail = [&r](Status s, const char* m) {
        r.status = s;
        r.message = m;
        return s;
    };

    AuthFields f;
    sv authorization, algorithm;
    const bool haveHeader = findHeader(req, "authorization", authorization);
    const bool haveQuery = findQuery(req, "X-Amz-Algorithm", algorithm);
    if (haveHeader && haveQuery) {
        return fail(Status::AccessDenied, "use either the Authorization header or a presigned URL, not both");
    }
    if (!haveHeader && !haveQuery) {
        return fail(Status::MissingAuthentication, "request is not signed (AWS SigV4 required)");
    }

    if (haveHeader) {
        const char* msg = "";
        if (!parseAuthorization(authorization, f, msg)) return fail(Status::AccessDenied, msg);
        if (!findHeader(req, "x-amz-date", f.amzDate)) {
            return fail(Status::AccessDenied, "x-amz-date header required");
        }
        sv content;
        if (findHeader(req, "x-amz-content-sha256", content)) {
            f.payloadHash = content;
            if (content == kUnsignedPayload) {
                r.payload = Payload::Unsigned;
            } else if (content == kStreamingPayload) {
                r.payload = Payload::Streaming;
            } else if (content == kStreamingUnsignedTrailer) {
                r.payload = Payload::UnsignedStreaming;
            } else if (fromHex(content, r.payloadSha256, 32)) {
                r.payload = Payload::Sha256;
            } else {
                return fail(Status::AccessDenied, "unsupported x-amz-content-sha256");
            }
        } else if (req.contentLength() == 0) {
            f.payloadHash = kEmptySha256;  // generic SigV4: the hash of the (empty) body
            r.payload = Payload::Sha256;
            fromHex(kEmptySha256, r.payloadSha256, 32);
        } else {
            return fail(Status::AccessDenied, "x-amz-content-sha256 header required");
        }
    } else {
        f.presigned = true;
        r.presigned = true;
        if (algorithm != kAlgorithm) return fail(Status::AccessDenied, "X-Amz-Algorithm must be AWS4-HMAC-SHA256");
        sv cred, expires;
        if (!findQuery(req, "X-Amz-Credential", cred) || !findQuery(req, "X-Amz-Date", f.amzDate) ||
            !findQuery(req, "X-Amz-Expires", expires) || !findQuery(req, "X-Amz-SignedHeaders", f.signedHeaders) ||
            !findQuery(req, "X-Amz-Signature", f.signatureHex)) {
            return fail(Status::AccessDenied,
                        "presigned URL needs X-Amz-Credential, -Date, -Expires, -SignedHeaders and -Signature");
        }
        if (!splitCredential(cred, f)) return fail(Status::AccessDenied, "malformed X-Amz-Credential");
        if (!parseUnsigned(expires, f.expires) || f.expires < 1 || f.expires > kMaxPresignExpires) {
            return fail(Status::AccessDenied, "X-Amz-Expires must be 1..604800");
        }
        f.payloadHash = cfg.presignedPayload;
        r.payload = Payload::Unsigned;
    }

    int64_t signedAt = 0;
    if (!parseAmzDate(f.amzDate, signedAt)) return fail(Status::AccessDenied, "malformed x-amz-date");
    memcpy(r.amzDate, f.amzDate.data(), 16);
    r.amzDate[16] = '\0';
    if (f.accessKeyId != cfg.accessKeyId) return fail(Status::AccessDenied, "unknown access key id");
    if (f.date8 != f.amzDate.substr(0, 8)) return fail(Status::AccessDenied, "credential date does not match x-amz-date");
    if (f.region != cfg.region) return fail(Status::AccessDenied, "credential region must be us-east-1");
    if (f.service != cfg.service) return fail(Status::AccessDenied, "credential service must be s3");
    if (f.terminator != kTerminator) return fail(Status::AccessDenied, "credential scope must end in aws4_request");
    if (!hasToken(f.signedHeaders, "host")) return fail(Status::AccessDenied, "host must be a signed header");
    uint8_t claimed[32];
    if (!fromHex(f.signatureHex, claimed, 32)) return fail(Status::AccessDenied, "malformed signature");

    // One SHA context for both steps (stack): hash(canonical request), then
    // HMAC(signing key, string to sign) into the same digest buffer.
    uint8_t digest[32];
    Sha sha;
    sha.init();
    {
        ShaSink<Sha> sink{sha};
        if (!writeCanonicalRequest(req, f.signedHeaders, f.payloadHash, f.presigned, sink)) {
            return fail(Status::AccessDenied, "a signed header is missing from the request");
        }
    }
    sha.finish(digest);
    {
        uint8_t key[32];
        if (!keyFor(f.date8, key)) return fail(Status::AccessDenied, "no signing key");
        Hmac<Sha> h(sha);
        h.begin(key, 32);
        memset(key, 0, sizeof key);
        writeStringToSign(h, f.amzDate, cfg.region, cfg.service, digest);
        h.finish(digest);
    }
    if (!digestEqual(digest, claimed, 32)) {
        return fail(Status::SignatureDoesNotMatch, "the request signature does not match");
    }
    memcpy(r.signature, claimed, 32);

    if (cfg.now > 0) {
        if (f.presigned) {
            if (signedAt - cfg.now > kMaxSkewSeconds) {
                return fail(Status::RequestTimeTooSkewed, "presigned URL is not valid yet");
            }
            if (cfg.now > signedAt + f.expires) return fail(Status::AccessDenied, "request has expired");
        } else {
            const int64_t d = cfg.now - signedAt;
            if (d > kMaxSkewSeconds || d < -kMaxSkewSeconds) {
                return fail(Status::RequestTimeTooSkewed,
                            "x-amz-date differs more than 15 minutes from the device time");
            }
        }
    }
    r.status = Status::Ok;
    r.message = "";
    return Status::Ok;
}

// ---------------------------------------------------------------------------
// Credentials
// ---------------------------------------------------------------------------

inline bool isBase32Char(char c) { return (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'); }
inline bool isAlnum(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

inline bool isValidAccessKeyId(sv s) {
    if (s.size() != kAccessKeyIdLen || s[0] != 'F' || s[1] != 'B') return false;
    for (size_t i = 2; i < s.size(); ++i) {
        if (!isBase32Char(s[i])) return false;
    }
    return true;
}

inline bool isValidSecret(sv s) {
    if (s.size() != kSecretLen) return false;
    for (char c : s) {
        if (!isAlnum(c)) return false;
    }
    return true;
}

// rng() -> uint8_t, uniformly random. Unbiased: base32 takes 5 of 8 bits;
// the secret rejects bytes >= 248 (= 4 * 62).
template <class Rng>
inline void generateCredentials(Rng&& rng, char akid[kAccessKeyIdLen + 1], char secret[kSecretLen + 1]) {
    static constexpr char kB32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    static constexpr char kAlnum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    akid[0] = 'F';
    akid[1] = 'B';
    for (size_t i = 2; i < kAccessKeyIdLen; ++i) akid[i] = kB32[static_cast<uint8_t>(rng()) & 31];
    akid[kAccessKeyIdLen] = '\0';
    for (size_t i = 0; i < kSecretLen;) {
        const auto b = static_cast<uint8_t>(rng());
        if (b >= 248) continue;
        secret[i++] = kAlnum[b % 62];
    }
    secret[kSecretLen] = '\0';
}

// "FBCRED:<akid>:<secret>" into out (kQrPayloadLen + 1 bytes).
inline bool formatQrPayload(sv akid, sv secret, char* out, size_t cap) {
    if (cap < kQrPayloadLen + 1 || akid.size() != kAccessKeyIdLen || secret.size() != kSecretLen) return false;
    char* p = out;
    memcpy(p, kQrPrefix.data(), kQrPrefix.size());
    p += kQrPrefix.size();
    memcpy(p, akid.data(), akid.size());
    p += akid.size();
    *p++ = ':';
    memcpy(p, secret.data(), secret.size());
    p += secret.size();
    *p = '\0';
    return true;
}

}  // namespace fbauth

#endif  // FREEBUTTON_AUTHLOGIC_H
