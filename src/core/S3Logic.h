//
// S3Logic - the pure parts of the minimal S3-compatible file API (S3Server):
// the bucket table (bucket -> LittleFS directory or media store bucket),
// URL -> bucket/key parsing, key validation and key -> path mapping, size
// limits, media store result -> HTTP mapping, the aws-chunked body decoder,
// XML/URL escaping written on the fly into a sink, ETag / date formatting and
// the ListObjects roll-up rules for the "/" delimiter.
//
// No Arduino / ESP headers and no heap: every helper writes into a sink with
// a Put(char) member (WindowOutputStream, or a test string) or into a caller
// buffer. Unit-tested natively in test/test_s3_logic.
//

#ifndef FREEBUTTON_S3LOGIC_H
#define FREEBUTTON_S3LOGIC_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../media/media_store.h"

namespace s3 {

// LittleFS here: name_max 64 per path component (mklittlefs image built with
// name_max=64, CONFIG_LITTLEFS_OBJ_NAME_LEN=64).
constexpr size_t kNameMax = 64;
// Longest key a client may write: "/" + key fits in 64 bytes.
constexpr size_t kWriteKeyMax = 63;
// Longest key (relative to its bucket's directory) the listing / GetObject
// handle (existing files, e.g. in bucket web).
constexpr size_t kReadKeyMax = 96;
// Longest bucket directory ("scripts/examples").
constexpr size_t kDirMax = 16;
// Largest object a PutObject accepts (decoded size) = MAX_JS_SOURCE_SIZE.
constexpr size_t kMaxObjectSize = 64 * 1024;
// Largest object a media store bucket (fonts) accepts.
constexpr size_t kMaxMediaObjectSize = 2 * 1024 * 1024;
constexpr const char* kTmpSuffix = ".tmp";
constexpr size_t kTmpSuffixLen = 4;

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

enum class KeyCheck : uint8_t {
    Ok,
    Invalid,      // 400 InvalidArgument / KeyTooLong
    Forbidden,    // 403 AccessDenied (protected path)
    ReadOnly,     // 403 AccessDenied (read-only bucket)
    OtherBucket,  // 400 InvalidArgument (the key lies in another bucket's directory)
};

inline bool startsWith(const char* s, size_t len, const char* prefix) {
    const size_t plen = strlen(prefix);
    return len >= plen && memcmp(s, prefix, plen) == 0;
}

inline bool endsWith(const char* s, size_t len, const char* suffix) {
    const size_t slen = strlen(suffix);
    return len >= slen && memcmp(s + len - slen, suffix, slen) == 0;
}

// ---------------------------------------------------------------------------
// Buckets
// ---------------------------------------------------------------------------
//
// One bucket per kind of content: a directory on the LittleFS partition, or
// a bucket of the media store (docs/MEDIA-STORE.md; then dir is "" and media
// >= 0: a flat key space of its own, outside LittleFS).
// Object keys are relative to the bucket's directory. A bucket's directory
// tree excludes the directories of the other buckets below it, so every
// object is reachable through exactly one bucket: in `app` (the root) the
// subtrees www/ and scripts/examples/ are not listed, not readable (404)
// and not writable (400). A LittleFS fonts/ directory is an ordinary part
// of `app`: the bucket `fonts` is in the media store.

constexpr size_t cLen(const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

struct Bucket {
    const char* name;
    const char* dir;  // relative to the mount point, no leading/trailing '/'; "" = root
    uint8_t nameLen;
    uint8_t dirLen;
    bool writable;
    // "/<name>/..." is also a URL of the web IDE (serveStatic from /www):
    // only requests from S3 clients go to the bucket.
    bool sharesStaticPath;
    // Reads (GET/HEAD objects, listings, HeadBucket, location) need no
    // signature. Everything else on the S3 API is SigV4-signed (Auth.h).
    bool publicRead;
    // media_bucket_t of a media store bucket (objects live in the `media`
    // partition, not on LittleFS); -1 = the LittleFS directory `dir`.
    int8_t media;

    constexpr Bucket(const char* n, const char* d, bool w, bool s, bool p, int8_t m = -1)
        : name(n), dir(d), nameLen(static_cast<uint8_t>(cLen(n))), dirLen(static_cast<uint8_t>(cLen(d))),
          writable(w), sharesStaticPath(s), publicRead(p), media(m) {}

    constexpr bool isMedia() const { return media >= 0; }
    media_bucket_t mediaBucket() const { return static_cast<media_bucket_t>(media); }
};

inline constexpr Bucket kBuckets[] = {
    // name       dir                 writable static  publicRead media
    {"app", "", true, false, false},                         // user data: config.json (secrets), scripts/, bytecode index
    {"web", "www", false, false, true},                      // the IDE bundle (gzipped)
    {"examples", "scripts/examples", false, false, true},    // example scripts
    {"fonts", "", true, true, true, MEDIA_BUCKET_FONTS},     // TrueType fonts (media store); the IDE loads /fonts/*.woff2 itself
};
inline constexpr size_t kBucketCount = sizeof(kBuckets) / sizeof(kBuckets[0]);

constexpr bool bucketTableValid() {
    for (const Bucket& b : kBuckets) {
        if (b.dirLen > kDirMax || b.nameLen == 0) return false;
        if (b.dirLen && (b.dir[0] == '/' || b.dir[b.dirLen - 1] == '/')) return false;
        if (b.isMedia() && (b.dirLen || b.media >= MEDIA_BUCKET_COUNT)) return false;
    }
    return true;
}
static_assert(bucketTableValid(), "bucket directories: at most kDirMax, no leading/trailing '/'; media buckets none");
static_assert(MEDIA_KEY_MAX <= kReadKeyMax, "media keys must fit the listing's key buffers");

// Requests that need no signature: reads of a publicly readable bucket,
// ListBuckets (b == nullptr) and requests for an unknown bucket (b ==
// nullptr, answered NoSuchBucket). Writes always need one, whatever bucket.
inline bool anonymousAllowed(const Bucket* b, bool isRead) {
    return isRead && (b == nullptr || b->publicRead);
}

// ListAllMyBucketsResult for kBuckets (the native tests check they match).
inline constexpr char kListBucketsXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<ListAllMyBucketsResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
    "<Owner><ID>freebutton</ID><DisplayName>freebutton</DisplayName></Owner><Buckets>"
    "<Bucket><Name>app</Name><CreationDate>1970-01-01T00:00:00.000Z</CreationDate></Bucket>"
    "<Bucket><Name>web</Name><CreationDate>1970-01-01T00:00:00.000Z</CreationDate></Bucket>"
    "<Bucket><Name>examples</Name><CreationDate>1970-01-01T00:00:00.000Z</CreationDate></Bucket>"
    "<Bucket><Name>fonts</Name><CreationDate>1970-01-01T00:00:00.000Z</CreationDate></Bucket>"
    "</Buckets></ListAllMyBucketsResult>";

inline const Bucket* findBucket(const char* name, size_t len) {
    for (const Bucket& b : kBuckets) {
        if (b.nameLen == len && memcmp(b.name, name, len) == 0) return &b;
    }
    return nullptr;
}

// The bucket other than b whose directory holds b's key (a file key, or a
// directory key ending in '/'), or nullptr when the key belongs to b. Used to
// reject such keys and to prune the walk before opening those directories.
inline const Bucket* keyOwner(const Bucket& b, const char* key, size_t len) {
    if (b.isMedia()) return nullptr;  // a key space of its own
    for (const Bucket& c : kBuckets) {
        if (&c == &b || c.isMedia() || c.dirLen <= b.dirLen) continue;
        // c.dir relative to b.dir, if c lies below b.
        size_t off = 0;
        if (b.dirLen) {
            if (memcmp(c.dir, b.dir, b.dirLen) != 0 || c.dir[b.dirLen] != '/') continue;
            off = b.dirLen + 1u;
        }
        const size_t subLen = c.dirLen - off;
        if (len > subLen && memcmp(key, c.dir + off, subLen) == 0 && key[subLen] == '/') return &c;
    }
    return nullptr;
}

// "<mount>/" or "<mount>/<dir>/" into out (NUL-terminated); returns the
// length, or 0 when it does not fit. The mount point may be empty (the
// firmware mounts LittleFS at ""), giving "/" and "/www/".
inline size_t bucketBase(const Bucket& b, const char* mount, size_t mountLen, char* out, size_t cap) {
    const size_t n = mountLen + 1 + (b.dirLen ? b.dirLen + 1u : 0);
    if (n + 1 > cap) return 0;
    memcpy(out, mount, mountLen);
    out[mountLen] = '/';
    if (b.dirLen) {
        memcpy(out + mountLen + 1, b.dir, b.dirLen);
        out[n - 1] = '/';
    }
    out[n] = '\0';
    return n;
}

// "<base><key><suffix>" into out; returns the length, or 0 when it does not fit.
inline size_t objectPath(const Bucket& b, const char* mount, size_t mountLen, const char* key, size_t keyLen,
                         const char* suffix, char* out, size_t cap) {
    const size_t base = bucketBase(b, mount, mountLen, out, cap);
    if (!base) return 0;
    const size_t slen = strlen(suffix);
    if (base + keyLen + slen + 1 > cap) return 0;
    memcpy(out + base, key, keyLen);
    memcpy(out + base + keyLen, suffix, slen + 1);
    return base + keyLen + slen;
}

// Path-style request target (the server has already URL-decoded the path).
struct UrlTarget {
    enum Type : uint8_t { Root, BucketRoot, Object, Unknown } type;
    const Bucket* bucket;  // BucketRoot / Object
    const char* key;       // Object: points into the URL
    size_t keyLen;
};

inline UrlTarget parseUrl(const char* u, size_t len) {
    if (len == 1 && u[0] == '/') return {UrlTarget::Root, nullptr, nullptr, 0};
    if (len < 2 || u[0] != '/') return {UrlTarget::Unknown, nullptr, nullptr, 0};
    size_t end = 1;
    while (end < len && u[end] != '/') ++end;
    const Bucket* b = findBucket(u + 1, end - 1);
    if (!b) return {UrlTarget::Unknown, nullptr, nullptr, 0};
    if (end + 1 >= len) return {UrlTarget::BucketRoot, b, nullptr, 0};  // "/app" or "/app/"
    return {UrlTarget::Object, b, u + end + 1, len - end - 1};
}

// Structural checks shared by reads and writes: non-empty, no leading or
// trailing '/', no empty segment ("//"), no ".." anywhere, no control bytes.
inline bool keyWellFormed(const char* key, size_t len, size_t maxLen) {
    if (!key || len == 0 || len > maxLen) return false;
    if (key[0] == '/' || key[len - 1] == '/') return false;
    size_t seg = 0;
    for (size_t i = 0; i < len; ++i) {
        const auto c = static_cast<unsigned char>(key[i]);
        if (c < 0x20 || c == 0x7F) return false;
        if (c == '.' && i + 1 < len && key[i + 1] == '.') return false;
        if (c == '/') {
            if (seg == 0) return false;
            seg = 0;
        } else if (++seg > kNameMax) {
            return false;
        }
    }
    return true;
}

// GetObject / HeadObject: well-formed and not in another bucket's directory.
inline bool keyReadable(const Bucket& b, const char* key, size_t len) {
    return keyWellFormed(key, len, kReadKeyMax) && !keyOwner(b, key, len);
}

// Internal metadata in bucket app: never written or deleted through the API.
inline bool keyProtected(const Bucket& b, const char* key, size_t len) {
    return b.dirLen == 0 && len == 21 && memcmp(key, "js_bytecode_meta.json", 21) == 0;
}

// PutObject / DeleteObject rules. kWriteKeyMax bounds the whole path below
// the mount point, so writable buckets must be the root (static_assert in
// S3Server).
inline bool keyCharOk(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-';
}

// Extensions a media bucket stores (lower case only, as in app).
inline bool mediaKeyExtensionOk(const Bucket& b, const char* key, size_t len) {
    if (b.media == MEDIA_BUCKET_FONTS) return endsWith(key, len, ".ttf") || endsWith(key, len, ".otf");
    return false;  // no other media bucket is exposed yet
}

// Media bucket keys: flat (no '/'), at most MEDIA_KEY_MAX of [A-Za-z0-9._-],
// no "..", a non-empty name plus a known extension.
// (Not LittleFS: no name_max per component.)
inline KeyCheck checkMediaKey(const Bucket& b, const char* key, size_t len) {
    if (!key || len == 0 || len > MEDIA_KEY_MAX) return KeyCheck::Invalid;
    for (size_t i = 0; i < len; ++i) {
        if (!keyCharOk(key[i])) return KeyCheck::Invalid;
        if (key[i] == '.' && i + 1 < len && key[i + 1] == '.') return KeyCheck::Invalid;
    }
    if (len < 5 || !mediaKeyExtensionOk(b, key, len)) return KeyCheck::Invalid;
    return KeyCheck::Ok;
}

inline KeyCheck checkWriteKey(const Bucket& b, const char* key, size_t len) {
    if (!b.writable) return KeyCheck::ReadOnly;
    if (b.isMedia()) return checkMediaKey(b, key, len);
    if (!keyWellFormed(key, len, kWriteKeyMax)) return KeyCheck::Invalid;
    if (keyOwner(b, key, len)) return KeyCheck::OtherBucket;
    if (keyProtected(b, key, len)) return KeyCheck::Forbidden;
    for (size_t i = 0; i < len; ++i) {
        if (!keyCharOk(key[i]) && key[i] != '/') return KeyCheck::Invalid;
    }
    // The "<name>.tmp" sibling written first must fit name_max too.
    size_t last = 0;
    while (last < len && key[len - 1 - last] != '/') ++last;
    if (last + kTmpSuffixLen > kNameMax) return KeyCheck::Invalid;
    if (!endsWith(key, len, ".js") && !endsWith(key, len, ".json") && !endsWith(key, len, ".txt")) {
        return KeyCheck::Invalid;
    }
    return KeyCheck::Ok;
}

inline bool keyIsConfig(const Bucket& b, const char* key, size_t len) {
    return !b.isMedia() && b.dirLen == 0 && len == 11 && memcmp(key, "config.json", 11) == 0;
}

// An HTTP error answer: status (0 = none), S3 error code, message.
struct HttpError {
    uint16_t status;
    const char* code;
    const char* msg;
};

// The answer to a PutObject / DeleteObject key that checkWriteKey refuses
// (status 0: allowed).
inline HttpError writeRefusal(const Bucket& b, const char* key, size_t len) {
    switch (checkWriteKey(b, key, len)) {
        case KeyCheck::Ok:
            return {0, nullptr, nullptr};
        case KeyCheck::ReadOnly:
            return {403, "AccessDenied", "bucket is read-only"};
        case KeyCheck::Forbidden:
            return {403, "AccessDenied", "this path is read-only"};
        case KeyCheck::OtherBucket:
            return {400, "InvalidArgument", "key lies in another bucket's directory (see ListBuckets)"};
        case KeyCheck::Invalid:
            break;
    }
    if (b.isMedia()) {
        if (len > MEDIA_KEY_MAX) return {400, "KeyTooLongError", "key longer than 96 characters"};
        return {400, "InvalidArgument", "key must be a flat name of [A-Za-z0-9._-], no '..', ending in .ttf or .otf"};
    }
    if (len > kWriteKeyMax) return {400, "KeyTooLongError", "key longer than 63 characters"};
    return {400, "InvalidArgument",
            "key must be [A-Za-z0-9._-/], no '..' or empty segments, and end in .js, .json or .txt"};
}

// GetObject / HeadObject key in a media bucket.
enum class MediaReadKey : uint8_t {
    Ok,
    Malformed,  // 400 InvalidArgument (as in the LittleFS buckets)
    Missing,    // 404 NoSuchKey: well-formed, but no such key can exist
};

inline MediaReadKey checkMediaReadKey(const char* key, size_t len) {
    if (!key || len == 0 || len > kReadKeyMax) return MediaReadKey::Malformed;
    bool slash = false;
    for (size_t i = 0; i < len; ++i) {
        const auto c = static_cast<unsigned char>(key[i]);
        if (c < 0x20 || c == 0x7F) return MediaReadKey::Malformed;
        if (c == '.' && i + 1 < len && key[i + 1] == '.') return MediaReadKey::Malformed;
        if (c == '/') slash = true;
    }
    if (len > MEDIA_KEY_MAX || slash) return MediaReadKey::Missing;
    return MediaReadKey::Ok;
}

// Largest decoded body a PutObject into b accepts, and the 413 message.
inline uint32_t objectSizeLimit(const Bucket& b) {
    return static_cast<uint32_t>(b.isMedia() ? kMaxMediaObjectSize : kMaxObjectSize);
}

inline const char* tooLargeMessage(const Bucket& b) {
    return b.isMedia() ? "object larger than 2 MiB" : "object larger than 64 KiB";
}

// PutObject body length. A plain body: Content-Length; aws-chunked:
// x-amz-decoded-content-length (Content-Length counts the encoding). The
// decoded length is used whenever given. Media buckets reserve the exact
// size before the first body byte, so there the length must be known;
// LittleFS buckets accept aws-chunked without it (capped while streaming).
struct PutLength {
    bool known;
    uint32_t expected;
    HttpError error;  // status 0: accepted
};

inline PutLength putLength(const Bucket& b, bool chunked, const char* decodedLength, bool hasContentLength,
                           uint64_t contentLength) {
    PutLength r{false, 0, {0, nullptr, nullptr}};
    const uint32_t limit = objectSizeLimit(b);
    uint64_t v = 0;
    if (decodedLength) {
        const char* p = decodedLength;
        while (*p == ' ') ++p;
        if (*p < '0' || *p > '9') {
            r.error = {400, "InvalidArgument", "invalid x-amz-decoded-content-length"};
            return r;
        }
        for (; *p >= '0' && *p <= '9'; ++p) {
            if (v <= 0xFFFFFFFFull) v = v * 10 + static_cast<uint64_t>(*p - '0');  // saturates above 2^32
        }
        while (*p == ' ') ++p;
        if (*p) {
            r.error = {400, "InvalidArgument", "invalid x-amz-decoded-content-length"};
            return r;
        }
    } else if (!chunked && hasContentLength) {
        v = contentLength;
    } else {
        if (b.isMedia() || !hasContentLength) {
            r.error = {411, "MissingContentLength", "Content-Length or x-amz-decoded-content-length required"};
        }
        return r;
    }
    if (v > limit) {
        r.error = {413, "EntityTooLarge", tooLargeMessage(b)};
        return r;
    }
    r.known = true;
    r.expected = static_cast<uint32_t>(v);
    return r;
}

// ---------------------------------------------------------------------------
// Media store results
// ---------------------------------------------------------------------------

enum class MediaOp : uint8_t {
    Read,    // media_open / media_stat / media_next
    Write,   // media_put_begin / _write / _commit
    Delete,  // media_delete
};

// The HTTP answer for a media store result (status 0: success). Deleting a
// missing key succeeds (S3 answers 204 either way). NO_SPACE has a generic
// message; S3Server replaces it with formatNoSpaceMessage().
inline HttpError mediaError(media_err_t e, MediaOp op) {
    switch (e) {
        case MEDIA_OK:
            return {0, nullptr, nullptr};
        case MEDIA_ERR_UNAVAILABLE:
            return {503, "ServiceUnavailable", "media store unavailable"};
        case MEDIA_ERR_NOT_FOUND:
            if (op == MediaOp::Delete) return {0, nullptr, nullptr};
            if (op == MediaOp::Write) return {500, "InternalError", "upload lost by the media store"};
            return {404, "NoSuchKey", "The specified key does not exist."};
        case MEDIA_ERR_NO_SPACE:
            return {507, "InsufficientStorage", "not enough free space in the media store"};
        case MEDIA_ERR_INVALID:
            if (op == MediaOp::Read) return {404, "NoSuchKey", "The specified key does not exist."};
            return {400, "InvalidArgument", "refused by the media store (key, size or state)"};
        case MEDIA_ERR_BUSY:
            return {503, "SlowDown", op == MediaOp::Read ? "too many open files, retry" : "media store busy, retry"};
        case MEDIA_ERR_SIZE:
            return {400, "IncompleteBody", "body length differs from the declared length"};
        case MEDIA_ERR_IO:
            return {500, "InternalError", "media flash I/O failed"};
    }
    return {500, "InternalError", "media store error"};
}

// "object of <needed> bytes does not fit: <free> bytes free, <reclaimable>
// reclaimable once released" into out (NUL-terminated, cut to cap).
inline void formatNoSpaceMessage(uint32_t needed, uint32_t freeBytes, uint32_t reclaimable, char* out, size_t cap) {
    if (!cap) return;
    size_t n = 0;
    auto emit = [&](char c) {
        if (n + 1 < cap) out[n++] = c;
    };
    auto emitStr = [&](const char* s) {
        while (*s) emit(*s++);
    };
    auto emitNum = [&](uint32_t v) {
        char tmp[10];
        size_t k = 0;
        do {
            tmp[k++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v);
        while (k) emit(tmp[--k]);
    };
    emitStr("object of ");
    emitNum(needed);
    emitStr(" bytes does not fit: ");
    emitNum(freeBytes);
    emitStr(" bytes free, ");
    emitNum(reclaimable);
    emitStr(" reclaimable once released");
    out[n] = '\0';
}

// HeadBucket and listing headers of a media bucket (decimal bytes, from
// media_usage()).
inline constexpr const char* kMediaTotalHeader = "x-fb-media-total";
inline constexpr const char* kMediaUsedHeader = "x-fb-media-used";
inline constexpr const char* kMediaFreeHeader = "x-fb-media-free";
inline constexpr const char* kMediaReclaimableHeader = "x-fb-media-reclaimable";

// The mtime stored with an upload: now when the clock is set, else 0.
inline uint32_t mediaMtime(bool clockValid, int64_t now) {
    if (!clockValid || now <= 0) return 0;
    return now > 0xFFFFFFFFll ? 0xFFFFFFFFu : static_cast<uint32_t>(now);
}

inline const char* contentTypeFor(const char* key, size_t len) {
    if (endsWith(key, len, ".json")) return "application/json";
    if (endsWith(key, len, ".js")) return "application/javascript";
    if (endsWith(key, len, ".txt")) return "text/plain";
    if (endsWith(key, len, ".html")) return "text/html";
    if (endsWith(key, len, ".gz")) return "application/gzip";
    if (endsWith(key, len, ".ttf")) return "font/ttf";
    if (endsWith(key, len, ".otf")) return "font/otf";
    return "application/octet-stream";
}

// ---------------------------------------------------------------------------
// Sink writers (Put(char) streams)
// ---------------------------------------------------------------------------

template <class S>
void put(S& s, const char* str) {
    while (*str) s.Put(*str++);
}

template <class S>
void put(S& s, const char* str, size_t len) {
    for (size_t i = 0; i < len; ++i) s.Put(str[i]);
}

template <class S>
void putUint(S& s, uint64_t v) {
    char tmp[21];
    size_t n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) s.Put(tmp[--n]);
}

template <class S>
void putXml(S& s, const char* str, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        switch (const char c = str[i]) {
            case '&': put(s, "&amp;"); break;
            case '<': put(s, "&lt;"); break;
            case '>': put(s, "&gt;"); break;
            case '"': put(s, "&quot;"); break;
            case '\'': put(s, "&apos;"); break;
            default: s.Put(c); break;
        }
    }
}

template <class S>
void putXml(S& s, const char* str) {
    putXml(s, str, strlen(str));
}

// S3 encoding-type=url: percent-encode everything but unreserved and '/'.
template <class S>
void putUrlEncoded(S& s, const char* str, size_t len) {
    static constexpr char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; ++i) {
        const auto c = static_cast<unsigned char>(str[i]);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                           c == '~' || c == '/';
        if (plain) {
            s.Put(static_cast<char>(c));
        } else {
            s.Put('%');
            s.Put(hex[c >> 4]);
            s.Put(hex[c & 0xF]);
        }
    }
}

template <class S>
void putHex(S& s, const char* str, size_t len) {
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        const auto c = static_cast<unsigned char>(str[i]);
        s.Put(hex[c >> 4]);
        s.Put(hex[c & 0xF]);
    }
}

// Continuation tokens are the hex of the last returned key: opaque, URL- and
// XML-safe, stateless. Returns the decoded length, or -1 if malformed/too long.
inline int hexDecode(const char* in, size_t len, char* out, size_t cap) {
    if (len % 2 || len / 2 >= cap) return -1;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < len; i += 2) {
        const int hi = nib(in[i]);
        const int lo = nib(in[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        const char c = static_cast<char>(hi << 4 | lo);
        if (c == '\0') return -1;
        out[i / 2] = c;
    }
    out[len / 2] = '\0';
    return static_cast<int>(len / 2);
}

// ---------------------------------------------------------------------------
// Base64 (Content-MD5, x-amz-checksum-crc32)
// ---------------------------------------------------------------------------

// Returns the number of bytes decoded, or -1 on malformed input / overflow.
inline int base64Decode(const char* in, size_t len, uint8_t* out, size_t cap) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    while (len && (in[len - 1] == ' ' || in[len - 1] == '\t')) --len;
    while (len && (*in == ' ' || *in == '\t')) { ++in; --len; }
    if (len == 0 || len % 4) return -1;
    size_t n = 0;
    for (size_t i = 0; i < len; i += 4) {
        const bool last = i + 4 == len;
        const int pad = last ? (in[i + 3] == '=') + (in[i + 2] == '=' && in[i + 3] == '=') : 0;
        int v[4];
        for (int k = 0; k < 4; ++k) {
            v[k] = (k >= 4 - pad) ? 0 : val(in[i + k]);
            if (v[k] < 0) return -1;
        }
        const uint32_t triple = static_cast<uint32_t>(v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3]);
        const int bytes = 3 - pad;
        if (n + bytes > cap) return -1;
        out[n++] = static_cast<uint8_t>(triple >> 16);
        if (bytes > 1) out[n++] = static_cast<uint8_t>(triple >> 8);
        if (bytes > 2) out[n++] = static_cast<uint8_t>(triple);
    }
    return static_cast<int>(n);
}

// x-amz-checksum-crc32 value: base64 of the big-endian CRC32 (IEEE, zlib).
inline bool parseCrc32B64(const char* in, size_t len, uint32_t* crc) {
    uint8_t b[4];
    if (base64Decode(in, len, b, sizeof b) != 4) return false;
    *crc = static_cast<uint32_t>(b[0]) << 24 | static_cast<uint32_t>(b[1]) << 16 |
           static_cast<uint32_t>(b[2]) << 8 | b[3];
    return true;
}

// out must hold 9 bytes ("AAAAAA==" + NUL).
inline void formatCrc32B64(uint32_t crc, char* out) {
    static constexpr char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t b[4] = {static_cast<uint8_t>(crc >> 24), static_cast<uint8_t>(crc >> 16),
                          static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc)};
    const uint32_t t0 = static_cast<uint32_t>(b[0]) << 16 | b[1] << 8 | b[2];
    out[0] = tbl[t0 >> 18 & 63];
    out[1] = tbl[t0 >> 12 & 63];
    out[2] = tbl[t0 >> 6 & 63];
    out[3] = tbl[t0 & 63];
    const uint32_t t1 = static_cast<uint32_t>(b[3]) << 16;
    out[4] = tbl[t1 >> 18 & 63];
    out[5] = tbl[t1 >> 12 & 63];
    out[6] = '=';
    out[7] = '=';
    out[8] = '\0';
}

// ---------------------------------------------------------------------------
// Dates and ETags
// ---------------------------------------------------------------------------

struct Civil {
    int year;
    unsigned month, day, hour, minute, second, weekday;  // weekday 0 = Sunday
};

// Howard Hinnant's days-from-civil inverse; no gmtime/TZ dependency.
// 32-bit math (no 64-bit division helpers on the ESP32): valid up to 2106.
inline Civil toCivil(int64_t t64) {
    const uint32_t t = t64 < 0 ? 0 : (t64 > 0xFFFFFFFFll ? 0xFFFFFFFFu : static_cast<uint32_t>(t64));
    const uint32_t days = t / 86400;
    const uint32_t secs = t % 86400;
    const uint32_t z = days + 719468;
    const uint32_t era = z / 146097;
    const uint32_t doe = z - era * 146097;
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    Civil c{};
    c.day = doy - (153 * mp + 2) / 5 + 1;
    c.month = mp < 10 ? mp + 3 : mp - 9;
    c.year = static_cast<int>(yoe + era * 400 + (c.month <= 2));
    c.hour = static_cast<unsigned>(secs / 3600);
    c.minute = static_cast<unsigned>(secs / 60 % 60);
    c.second = static_cast<unsigned>(secs % 60);
    c.weekday = static_cast<unsigned>((days + 4) % 7);  // 1970-01-01 was a Thursday
    return c;
}

inline void put2(char* p, unsigned v) {
    p[0] = static_cast<char>('0' + v / 10 % 10);
    p[1] = static_cast<char>('0' + v % 10);
}

// "2026-11-08T12:34:56.000Z" (24 chars + NUL).
inline void formatIso8601(int64_t t, char out[25]) {
    const Civil c = toCivil(t);
    const unsigned y = static_cast<unsigned>(c.year) % 10000;
    put2(out, y / 100);
    put2(out + 2, y % 100);
    out[4] = '-';
    put2(out + 5, c.month);
    out[7] = '-';
    put2(out + 8, c.day);
    out[10] = 'T';
    put2(out + 11, c.hour);
    out[13] = ':';
    put2(out + 14, c.minute);
    out[16] = ':';
    put2(out + 17, c.second);
    memcpy(out + 19, ".000Z", 6);
}

// RFC 7231 IMF-fixdate "Sun, 08 Nov 2026 12:34:56 GMT" (29 chars + NUL).
inline void formatHttpDate(int64_t t, char out[30]) {
    static constexpr char days[] = "SunMonTueWedThuFriSat";
    static constexpr char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const Civil c = toCivil(t);
    memcpy(out, days + 3 * c.weekday, 3);
    out[3] = ',';
    out[4] = ' ';
    put2(out + 5, c.day);
    out[7] = ' ';
    memcpy(out + 8, months + 3 * (c.month - 1), 3);
    out[11] = ' ';
    const unsigned y = static_cast<unsigned>(c.year) % 10000;
    put2(out + 12, y / 100);
    put2(out + 14, y % 100);
    out[16] = ' ';
    put2(out + 17, c.hour);
    out[19] = ':';
    put2(out + 20, c.minute);
    out[22] = ':';
    put2(out + 23, c.second);
    memcpy(out + 25, " GMT", 5);
}

// "\"<size-hex>-<mtime-hex>\"" including the quotes. The '-' marks it as a
// non-MD5 ETag, so S3 clients do not verify it against content. Needs 20 B.
inline void formatETag(uint64_t size, uint64_t mtime, char out[40]) {
    static constexpr char hex[] = "0123456789abcdef";
    size_t n = 0;
    out[n++] = '"';
    auto putHexNum = [&](uint64_t v) {
        char tmp[16];
        size_t k = 0;
        do {
            tmp[k++] = hex[v & 0xF];
            v >>= 4;
        } while (v);
        while (k) out[n++] = tmp[--k];
    };
    putHexNum(size);
    out[n++] = '-';
    putHexNum(mtime);
    out[n++] = '"';
    out[n] = '\0';
}

// "\"<32 hex digits>\"" including the quotes: the MD5 of the content, the
// ETag S3 gives a non-multipart object (media buckets store it). 35 B.
inline void formatMd5ETag(const uint8_t md5[16], char out[35]) {
    static constexpr char hex[] = "0123456789abcdef";
    out[0] = '"';
    for (int i = 0; i < 16; ++i) {
        out[1 + 2 * i] = hex[md5[i] >> 4];
        out[2 + 2 * i] = hex[md5[i] & 0xF];
    }
    out[33] = '"';
    out[34] = '\0';
}

// ---------------------------------------------------------------------------
// ListObjects roll-up ("/" delimiter)
// ---------------------------------------------------------------------------

// Length of the value a file key is listed as: the key itself, or (with the
// delimiter) the common prefix up to and including the first '/' at or after
// the prefix. Caller has checked that key starts with the prefix.
inline size_t rollup(const char* key, size_t keyLen, size_t prefixLen, bool delimiter, bool* isPrefix) {
    *isPrefix = false;
    if (delimiter) {
        for (size_t i = prefixLen; i < keyLen; ++i) {
            if (key[i] == '/') {
                *isPrefix = true;
                return i + 1;
            }
        }
    }
    return keyLen;
}

enum class DirAction : uint8_t { Skip, Descend, Rollup };

// What the listing does with a directory "dir/" (dirKey ends with '/').
// Rollup: every file below it lists as the same common prefix, of length
// *rolledLen, so it need not be walked (only checked for being non-empty).
inline DirAction dirAction(const char* dirKey, size_t dirLen, const char* prefix, size_t prefixLen,
                           bool delimiter, size_t* rolledLen) {
    if (dirLen >= prefixLen && memcmp(dirKey, prefix, prefixLen) == 0) {
        if (delimiter && prefixLen < dirLen) {
            bool isPrefix;
            *rolledLen = rollup(dirKey, dirLen, prefixLen, true, &isPrefix);
            return DirAction::Rollup;
        }
        return DirAction::Descend;
    }
    if (prefixLen > dirLen && memcmp(prefix, dirKey, dirLen) == 0) return DirAction::Descend;
    return DirAction::Skip;
}

// Every value listed below dirKey starts with dirKey. Returns true when all
// of them sort <= lowerExclusive (nothing new there) - used to prune walks.
inline bool subtreeNotAbove(const char* dirKey, size_t dirLen, const char* lowerExclusive) {
    if (!lowerExclusive || !*lowerExclusive) return false;
    const size_t mlen = strlen(lowerExclusive);
    if (mlen >= dirLen && memcmp(lowerExclusive, dirKey, dirLen) == 0) return false;  // marker inside
    return strncmp(dirKey, lowerExclusive, dirLen) < 0;
}

// True when all values below dirKey sort > upper (cannot beat the best so far).
inline bool subtreeAbove(const char* dirKey, size_t dirLen, const char* upper) {
    if (!upper) return false;
    const size_t ulen = strlen(upper);
    if (ulen >= dirLen && memcmp(upper, dirKey, dirLen) == 0) return false;
    return strncmp(dirKey, upper, dirLen) > 0 || (ulen < dirLen && memcmp(dirKey, upper, ulen) == 0);
}

// ---------------------------------------------------------------------------
// aws-chunked body decoder
// ---------------------------------------------------------------------------
//
//   <hex-size>[;chunk-signature=<sig>]\r\n<data>\r\n ... 0[;...]\r\n
//   [<trailer-name>:<value>\r\n]*\r\n
//
// Streaming: bytes may arrive split anywhere; data is handed to the callback
// as spans of the input (zero copy). The only trailer kept is
// x-amz-checksum-crc32 (8 base64 chars), matched without buffering the name.
class AwsChunkedDecoder {
public:
    enum class State : uint8_t {
        Size, Ext, SizeLf, Data, DataCr, DataLf, TrailerStart, Trailer, TrailerLf, FinalLf, Done, Error
    };

    // onData(const uint8_t* data, size_t len). Returns false once in Error.
    template <class F>
    bool feed(const uint8_t* in, size_t len, F&& onData) {
        return feed(in, len, onData, [](const char*, size_t) { return true; });
    }

    // Also calls onChunkEnd(const char* sig, size_t sigLen) -> bool once per
    // chunk, after its data (the final empty chunk included), with the value
    // of its ";chunk-signature=" extension ("" when absent or malformed);
    // returning false puts the decoder in Error.
    template <class F, class E>
    bool feed(const uint8_t* in, size_t len, F&& onData, E&& onChunkEnd) {
        size_t i = 0;
        while (i < len && state_ != State::Error) {
            if (state_ == State::Data) {
                size_t span = len - i;
                if (span > remaining_) span = remaining_;
                onData(in + i, span);
                i += span;
                remaining_ -= static_cast<uint32_t>(span);
                if (remaining_ == 0) {
                    state_ = State::DataCr;
                    if (!onChunkEnd(sig_, sigLen_)) state_ = State::Error;
                }
                continue;
            }
            step(static_cast<char>(in[i++]));
            if (chunkEnded_) {  // the final, empty chunk (no Data state)
                chunkEnded_ = false;
                if (!onChunkEnd(sig_, sigLen_)) state_ = State::Error;
            }
        }
        return state_ != State::Error;
    }

    bool done() const { return state_ == State::Done; }
    bool failed() const { return state_ == State::Error; }
    State state() const { return state_; }

    bool hasCrc32() const { return haveCrc_; }
    const char* crc32B64() const { return crc_; }

private:
    static constexpr const char* kCrcName = "x-amz-checksum-crc32";
    static constexpr uint8_t kCrcNameLen = 20;
    static constexpr const char* kSigPrefix = "chunk-signature=";
    static constexpr uint8_t kSigPrefixLen = 16;
    static constexpr uint8_t kSigLen = 64;  // hex SHA-256 HMAC
    static constexpr uint8_t kNoSig = 0xFF;  // extension is not (or no longer) a signature

    State state_ = State::Size;
    uint32_t remaining_ = 0;
    uint8_t digits_ = 0;
    uint8_t extIdx_ = 0;
    uint8_t sigLen_ = 0;
    bool chunkEnded_ = false;
    char sig_[kSigLen + 1] = {};
    // trailer line matching
    uint8_t nameIdx_ = 0;
    bool nameMatch_ = true;
    bool inValue_ = false;
    bool haveCrc_ = false;
    uint8_t crcLen_ = 0;
    char crc_[13] = {};

    static int hexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    void startTrailerLine() {
        nameIdx_ = 0;
        nameMatch_ = true;
        inValue_ = false;
    }

    void trailerChar(char c) {
        if (!inValue_) {
            if (c == ':') {
                inValue_ = true;
                if (nameMatch_ && nameIdx_ == kCrcNameLen) crcLen_ = 0;
                return;
            }
            const char lc = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            if (nameIdx_ >= kCrcNameLen || kCrcName[nameIdx_] != lc) nameMatch_ = false;
            if (nameIdx_ < 255) ++nameIdx_;
            return;
        }
        if (nameMatch_ && nameIdx_ == kCrcNameLen && c != ' ' && c != '\t') {
            if (crcLen_ < sizeof(crc_) - 1) crc_[crcLen_++] = c;
        }
    }

    void endTrailerLine() {
        if (inValue_ && nameMatch_ && nameIdx_ == kCrcNameLen) {
            crc_[crcLen_] = '\0';
            haveCrc_ = crcLen_ > 0;
        }
    }

    void step(char c) {
        switch (state_) {
            case State::Size: {
                const int v = hexVal(c);
                if (v >= 0) {
                    if (digits_ == 0) sigLen_ = 0;  // new chunk header
                    if (remaining_ > 0x0FFFFFFF) { state_ = State::Error; return; }
                    remaining_ = remaining_ << 4 | static_cast<uint32_t>(v);
                    ++digits_;
                } else if (c == ';' && digits_) {
                    state_ = State::Ext;
                    extIdx_ = 0;
                } else if (c == '\r' && digits_) {
                    state_ = State::SizeLf;
                } else {
                    state_ = State::Error;
                }
                return;
            }
            case State::Ext:
                if (c == '\r') { state_ = State::SizeLf; return; }
                if (c == '\n') { state_ = State::Error; return; }
                if (extIdx_ < kSigPrefixLen) {
                    extIdx_ = c == kSigPrefix[extIdx_] ? static_cast<uint8_t>(extIdx_ + 1) : kNoSig;
                } else if (extIdx_ == kSigPrefixLen) {
                    if (sigLen_ < kSigLen) {
                        sig_[sigLen_++] = c;
                    } else {
                        sigLen_ = 0;  // too long: not a signature
                        extIdx_ = kNoSig;
                    }
                }
                return;
            case State::SizeLf:
                if (c != '\n') { state_ = State::Error; return; }
                digits_ = 0;
                if (remaining_ == 0) {
                    state_ = State::TrailerStart;
                    chunkEnded_ = true;
                } else {
                    state_ = State::Data;
                }
                return;
            case State::DataCr:
                state_ = c == '\r' ? State::DataLf : State::Error;
                return;
            case State::DataLf:
                state_ = c == '\n' ? State::Size : State::Error;
                return;
            case State::TrailerStart:
                if (c == '\r') { state_ = State::FinalLf; return; }
                if (c == '\n') { state_ = State::Error; return; }
                startTrailerLine();
                state_ = State::Trailer;
                trailerChar(c);
                return;
            case State::Trailer:
                if (c == '\r') { endTrailerLine(); state_ = State::TrailerLf; return; }
                if (c == '\n') { state_ = State::Error; return; }
                trailerChar(c);
                return;
            case State::TrailerLf:
                state_ = c == '\n' ? State::TrailerStart : State::Error;
                return;
            case State::FinalLf:
                state_ = c == '\n' ? State::Done : State::Error;
                return;
            case State::Done:
                return;  // ignore anything after the terminator
            case State::Data:
            case State::Error:
                return;
        }
    }
};

}  // namespace s3

#endif  // FREEBUTTON_S3LOGIC_H
