//
// S3Server - minimal S3-compatible API over LittleFS.
//
// Path-style addressing, one bucket per kind of content, each a directory on
// the LittleFS partition (table: s3::kBuckets in S3Logic.h):
//
//   app       /  minus the other buckets' directories   read/write
//   web       /www (the IDE bundle, gzipped)             read-only
//   examples  /scripts/examples                          read-only
//   fonts     /fonts (device fonts)                      read-only
//
// Object key = path relative to the bucket's directory ("config.json",
// "scripts/main.js" in app; "index.html.gz" in web; "button.js" in examples).
// Every object is reachable through exactly one bucket: app does not list
// www/, fonts/ or scripts/examples/ (the walk skips them without opening
// them), answers 404 NoSuchKey for reads and 400 InvalidArgument for writes
// of such keys.
//
//   GET  /                      ListBuckets (only for S3 clients: Authorization
//                               AWS*, any x-amz-* header, or ?x-id=...; browsers
//                               keep getting the web IDE)
//   HEAD /<b>[/]                HeadBucket
//   GET  /<b>[/]?list-type=2    ListObjectsV2 (prefix, delimiter "/", max-keys,
//                               continuation-token, start-after, encoding-type)
//   GET  /<b>[/]                ListObjects v1 (marker)
//   GET  /<b>[/]?location       GetBucketLocation
//   GET|HEAD /<b>/<key>         GetObject / HeadObject (Range ignored)
//   PUT  /<b>/<key>             PutObject (plain or aws-chunked body; app only,
//                               read-only buckets answer 403 AccessDenied)
//   DELETE /<b>/<key>           DeleteObject (app only, as PUT)
//   OPTIONS /<b>[/...]          CORS preflight
//
// /fonts/... is also the URL of the IDE's own web fonts (www/fonts/*.woff2):
// GET/HEAD there reach bucket fonts only from S3 clients (see isS3Request);
// a browser gets the IDE file. Unknown buckets: 404 NoSuchBucket (S3 clients).
//
// No authentication: Authorization headers are accepted and ignored.
//
// Memory: per request one small fixed struct in request->_tempObject
// (malloc'd; the server free()s it), nothing that grows with the number of
// files or the object size. Listings are generated on the fly into the
// chunked response buffer; object bodies go straight between the TCP buffers
// and LittleFS. All handlers run on the async_tcp task, so the slot counters
// below need no locking.
//

// RapidJSON's default assert() embeds the full template signature of every
// parse function as a string (~400 B each, ~4 KB for the validator's
// instantiations). Same check, no strings.
#ifndef RAPIDJSON_ASSERT
#include <cstdlib>
#define RAPIDJSON_ASSERT(x) ((x) ? (void)0 : abort())
#endif

#include "S3Server.h"

#include "S3Logic.h"
#include "json/JsonValidator.h"
#include "json/WindowOutputStream.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <dirent.h>
#include <errno.h>
#include <esp_log.h>
#include <esp_rom_crc.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <mbedtls/md5.h>
#include <new>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static auto TAG = "s3";

namespace {

using s3::put;

constexpr const char* kXmlDecl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
constexpr const char* kXmlNs = " xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"";
constexpr const char* kExposeHeaders = "ETag, x-amz-checksum-crc32, x-freebutton-reboot-required";
constexpr size_t kMountMax = 15;   // ESP_VFS_PATH_MAX
constexpr int kMaxDepth = 6;       // directory levels walked by a listing
constexpr uint8_t kMaxGets = 2;    // concurrent GetObject file handles
constexpr int kSearchesPerFill = 4;

// PutState::path and kWriteKeyMax assume writes go to the LittleFS root.
constexpr bool writableBucketsAtRoot() {
    for (const s3::Bucket& b : s3::kBuckets) {
        if (b.writable && b.dirLen) return false;
    }
    return true;
}
static_assert(writableBucketsAtRoot(), "a writable bucket below the root needs a larger PutState::path");

static constexpr char kLocationXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<LocationConstraint xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"/>";

enum class Kind : uint8_t { Get = 1, Put, List };

bool s_putActive = false;
uint8_t s_getsActive = 0;

// ---------------------------------------------------------------------------
// Per-request state (lives in request->_tempObject)
// ---------------------------------------------------------------------------

struct GetState {
    Kind kind = Kind::Get;
    int fd = -1;
};

struct PutState {
    Kind kind = Kind::Put;
    bool chunked = false;
    bool lengthKnown = false;
    bool haveMd5 = false;
    bool haveCrc = false;
    bool isConfig = false;
    bool ownsSlot = false;
    bool committed = false;
    uint16_t status = 0;  // != 0: request failed, respond with this
    const char* errCode = nullptr;
    const char* errMsg = nullptr;
    int fd = -1;
    uint32_t expected = 0;
    uint32_t written = 0;
    uint32_t crc = 0;
    uint32_t wantCrc = 0;
    uint32_t jsonOffset = 0;
    uint8_t wantMd5[16] = {};
    uint8_t finalLen = 0;  // path length without ".tmp"
    s3::AwsChunkedDecoder dec;
    mbedtls_md5_context md5;
    char path[kMountMax + 1 + s3::kWriteKeyMax + s3::kTmpSuffixLen + 1] = {};

    void fail(uint16_t s, const char* code, const char* msg) {
        if (status) return;  // keep the first error
        status = s;
        errCode = code;
        errMsg = msg;
    }
};

struct ListState {
    enum Phase : uint8_t { Header, Items, Footer, Done };
    enum Piece : uint8_t { None, HeaderPiece, ItemPiece, FooterPiece };
    enum MarkerFrom : uint8_t { NoMarker, Token, StartAfter, V1Marker };

    Kind kind = Kind::List;
    Phase phase = Header;
    Piece piece = None;
    MarkerFrom markerFrom = NoMarker;
    bool v2 = true;
    bool delimiter = false;
    bool urlEncode = false;
    bool truncated = false;
    bool noMatch = false;
    bool hasMarker = false;
    bool haveBest = false;
    bool bestIsPrefix = false;
    bool itemIsPrefix = false;
    uint8_t baseLen = 0;  // "<mount>/" or "<mount>/<dir>/"
    uint8_t prefixLen = 0;
    uint16_t maxKeys = 1000;
    uint16_t emitted = 0;
    uint16_t searches = 0;
    uint32_t pieceOff = 0;
    uint32_t itemSize = 0;
    int64_t itemMtime = 0;
    int64_t startedUs = 0;
    const s3::Bucket* bucket = nullptr;
    char prefix[s3::kReadKeyMax + 1] = {};
    char marker[s3::kReadKeyMax + 1] = {};
    char best[s3::kReadKeyMax + 1] = {};
    // "<mount>/[<dir>/]<rel>": the walk appends names to rel (= the key) in place.
    char path[kMountMax + 1 + s3::kDirMax + 1 + s3::kReadKeyMax + 2] = {};

    char* rel() { return path + baseLen; }
};

template <class T>
T* allocState(AsyncWebServerRequest* request) {
    void* mem = malloc(sizeof(T));  // freed by ~AsyncWebServerRequest
    if (!mem) return nullptr;
    T* st = new (mem) T();
    request->_tempObject = st;
    return st;
}

// Releases file handles and slots; idempotent. Runs from onDisconnect, which
// the server calls for every request before destroying it (normal end,
// client abort, timeout).
void releaseState(AsyncWebServerRequest* request) {
    auto* kind = static_cast<Kind*>(request->_tempObject);
    if (!kind) return;
    if (*kind == Kind::Get) {
        auto* g = reinterpret_cast<GetState*>(kind);
        if (g->fd >= 0) {
            close(g->fd);
            g->fd = -1;
            if (s_getsActive) --s_getsActive;
        }
    } else if (*kind == Kind::Put) {
        auto* p = reinterpret_cast<PutState*>(kind);
        if (p->fd >= 0) {
            close(p->fd);
            p->fd = -1;
        }
        if (!p->committed && p->path[0]) {
            unlink(p->path);  // the .tmp file
            p->path[0] = '\0';
        }
        if (p->haveMd5) {
            mbedtls_md5_free(&p->md5);
            p->haveMd5 = false;
        }
        if (p->ownsSlot) {
            s_putActive = false;
            p->ownsSlot = false;
        }
    }
}

void watchState(AsyncWebServerRequest* request) {
    request->onDisconnect([request]() { releaseState(request); });
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Length of the LittleFS mount point copied into out, or -1 when the
// filesystem is not mounted. The firmware mounts it with an EMPTY base path
// (main.cpp: initBusAwareFileSystem(..., "", ...)), so 0 is valid: paths are
// then "/config.json".
int mountPoint(char* out) {
    const char* m = LittleFS.mountpoint();
    if (!m) return -1;
    const size_t n = strlen(m);
    if (n > kMountMax) return -1;
    memcpy(out, m, n);
    out[n] = '\0';
    return static_cast<int>(n);
}

// "<mount>/[<dir>/]<key><suffix>" into out; returns the length or 0.
size_t buildPath(const s3::Bucket& b, char* out, size_t cap, const char* key, size_t keyLen,
                 const char* suffix = "") {
    char mount[kMountMax + 1];
    const int mp = mountPoint(mount);
    if (mp < 0) return 0;
    return s3::objectPath(b, mount, static_cast<size_t>(mp), key, keyLen, suffix, out, cap);
}

void addCommonHeaders(AsyncWebServerResponse* response) {
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Expose-Headers", kExposeHeaders);
}

void sendXmlError(AsyncWebServerRequest* request, int status, const char* code, const char* message) {
    AsyncWebServerResponse* response;
    if (request->method() == HTTP_HEAD) {
        response = request->beginResponse(status);  // HEAD: no body
    } else {
        char body[256];
        WindowOutputStream w(reinterpret_cast<uint8_t*>(body), 0, sizeof(body) - 1);
        put(w, kXmlDecl);
        put(w, "<Error><Code>");
        put(w, code);
        put(w, "</Code><Message>");
        s3::putXml(w, message);
        put(w, "</Message></Error>");
        body[w.copied()] = '\0';
        response = request->beginResponse(status, "application/xml", body);
    }
    addCommonHeaders(response);
    request->send(response);
}

void sendConstXml(AsyncWebServerRequest* request, const char* xml, size_t len) {
    AsyncWebServerResponse* response =
        request->beginResponse(200, "application/xml", reinterpret_cast<const uint8_t*>(xml), len);
    addCommonHeaders(response);
    request->send(response);
}

// S3 clients, as opposed to a browser: SigV2/V4 Authorization, any x-amz-*
// header, the AWS SDKs' amz-sdk-* headers, or a known S3 tool User-Agent
// (unsigned requests - aws --no-sign-request, rclone anonymous - carry no
// Authorization and not always an x-amz-* header).
bool isS3Client(AsyncWebServerRequest* request) {
    static constexpr const char* kAgents[] = {"aws-cli", "Boto3", "Botocore", "aws-sdk", "rclone", "MinIO", "S3"};
    const size_t n = request->headers();
    for (size_t i = 0; i < n; ++i) {
        const AsyncWebHeader* h = request->getHeader(i);
        if (!h) continue;
        const String& name = h->name();
        if (name.length() > 6 && strncasecmp(name.c_str(), "x-amz-", 6) == 0) return true;
        if (name.length() > 8 && strncasecmp(name.c_str(), "amz-sdk-", 8) == 0) return true;
        if (name.equalsIgnoreCase("Authorization") && strncmp(h->value().c_str(), "AWS", 3) == 0) return true;
        if (name.equalsIgnoreCase("User-Agent")) {
            for (const char* agent : kAgents) {
                if (strncmp(h->value().c_str(), agent, strlen(agent)) == 0) return true;
            }
        }
    }
    return false;
}

const char* headerValue(AsyncWebServerRequest* request, const char* name) {
    const AsyncWebHeader* h = request->getHeader(name);
    return h ? h->value().c_str() : nullptr;
}

// An S3 client, or a request only an S3 client sends (SDK GetObject & co add
// ?x-id=..., listings carry list-type / location).
bool isS3Request(AsyncWebServerRequest* request) {
    return request->hasParam("x-id") || request->hasParam("list-type") || request->hasParam("location") ||
           isS3Client(request);
}

const String* paramValue(AsyncWebServerRequest* request, const char* name) {
    const AsyncWebParameter* p = request->getParam(name);
    return p ? &p->value() : nullptr;
}

using Target = s3::UrlTarget;

// The server has already URL-decoded the path.
Target parseTarget(const String& url) {
    return s3::parseUrl(url.c_str(), url.length());
}

void formatStatHeaders(AsyncWebServerResponse* response, const struct stat& st) {
    char etag[40];
    s3::formatETag(static_cast<uint64_t>(st.st_size), static_cast<uint64_t>(st.st_mtime), etag);
    response->addHeader("ETag", etag);
    char date[30];
    s3::formatHttpDate(static_cast<int64_t>(st.st_mtime), date);
    response->addHeader("Last-Modified", date);
}

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------

// Compares a[0..alen) with the C string b, bytewise unsigned (S3 key order).
int cmpN(const char* a, size_t alen, const char* b) {
    const size_t blen = strlen(b);
    const int r = memcmp(a, b, alen < blen ? alen : blen);
    if (r) return r;
    return alen < blen ? -1 : (alen > blen ? 1 : 0);
}

DIR* openRelDir(ListState* s, size_t relLen) {
    char* rel = s->rel();
    DIR* d;
    if (relLen == 0) {
        // Bucket root: "<mount>", "/" (empty mount point) or "<mount>/<dir>".
        const size_t end = s->baseLen > 1 ? s->baseLen - 1u : 1u;
        const char saved = s->path[end];
        s->path[end] = '\0';
        d = opendir(s->path);
        s->path[end] = saved;
    } else {
        rel[relLen - 1] = '\0';  // drop the trailing '/'
        d = opendir(s->path);
        rel[relLen - 1] = '/';
    }
    return d;
}

bool isDot(const char* n) {
    return n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0'));
}

// The directory key rel[0..len) is another bucket's directory (app: www/,
// fonts/, scripts/examples/): never opened by this bucket's walk.
bool otherBucketsDir(const ListState* s, const char* rel, size_t len) {
    return s3::keyOwner(*s->bucket, rel, len) != nullptr;
}

// True when the directory rel[0..relLen) contains at least one file.
bool dirHasFile(ListState* s, size_t relLen, int depth) {
    if (depth >= kMaxDepth) return false;
    DIR* d = openRelDir(s, relLen);
    if (!d) return false;
    char* rel = s->rel();
    bool found = false;
    while (!found) {
        const dirent* e = readdir(d);
        if (!e) break;
        if (isDot(e->d_name)) continue;
        if (e->d_type == DT_REG) {
            found = true;
        } else if (e->d_type == DT_DIR) {
            const size_t n = strlen(e->d_name);
            if (relLen + n + 1 > s3::kReadKeyMax) continue;
            memcpy(rel + relLen, e->d_name, n);
            rel[relLen + n] = '/';
            if (otherBucketsDir(s, rel, relLen + n + 1)) continue;
            found = dirHasFile(s, relLen + n + 1, depth + 1);
        }
    }
    closedir(d);
    return found;
}

// Offers value[0..len) (rel prefix) as the next listing entry.
void consider(ListState* s, size_t len, bool isPrefix, size_t dirLenToCheck, int depth) {
    const char* value = s->rel();
    if (s->hasMarker && cmpN(value, len, s->marker) <= 0) return;
    if (s->haveBest && cmpN(value, len, s->best) >= 0) return;
    if (dirLenToCheck && !dirHasFile(s, dirLenToCheck, depth + 1)) return;
    memcpy(s->best, value, len);  // dirHasFile left rel[0..dirLen) intact
    s->best[len] = '\0';
    s->bestIsPrefix = isPrefix;
    s->haveBest = true;
}

void scanDir(ListState* s, size_t relLen, int depth) {
    DIR* d = openRelDir(s, relLen);
    if (!d) return;
    char* rel = s->rel();
    while (const dirent* e = readdir(d)) {
        if (isDot(e->d_name)) continue;
        const bool isDir = e->d_type == DT_DIR;
        if (!isDir && e->d_type != DT_REG) continue;
        const size_t n = strlen(e->d_name);
        const size_t keyLen = relLen + n + (isDir ? 1 : 0);
        if (keyLen > s3::kReadKeyMax) {
            ESP_LOGW(TAG, "list: skipping over-long path under '%.*s'", static_cast<int>(relLen), rel);
            continue;
        }
        memcpy(rel + relLen, e->d_name, n);
        if (isDir) rel[relLen + n] = '/';
        rel[keyLen] = '\0';

        if (isDir) {
            if (depth + 1 >= kMaxDepth) continue;
            if (otherBucketsDir(s, rel, keyLen)) continue;
            if (s->hasMarker && s3::subtreeNotAbove(rel, keyLen, s->marker)) continue;
            if (s->haveBest && s3::subtreeAbove(rel, keyLen, s->best)) continue;
            size_t rolled = 0;
            switch (s3::dirAction(rel, keyLen, s->prefix, s->prefixLen, s->delimiter, &rolled)) {
                case s3::DirAction::Skip:
                    break;
                case s3::DirAction::Descend:
                    scanDir(s, keyLen, depth + 1);
                    break;
                case s3::DirAction::Rollup:
                    consider(s, rolled, true, keyLen, depth);
                    break;
            }
        } else {
            if (keyLen < s->prefixLen || memcmp(rel, s->prefix, s->prefixLen) != 0) continue;
            bool isPrefix = false;
            const size_t rl = s3::rollup(rel, keyLen, s->prefixLen, s->delimiter, &isPrefix);
            consider(s, rl, isPrefix, 0, depth);
        }
    }
    closedir(d);
    rel[relLen] = '\0';
}

// Finds the smallest listing value > marker into best. Stateless between
// calls: one walk per entry, pruned to the subtrees that can hold it.
bool findNext(ListState* s) {
    s->haveBest = false;
    ++s->searches;
    if (s->noMatch) return false;
    s->rel()[0] = '\0';
    scanDir(s, 0, 0);
    return s->haveBest;
}

template <class S>
void putText(const ListState* s, S& w, const char* str, size_t len) {
    if (s->urlEncode) s3::putUrlEncoded(w, str, len);
    else s3::putXml(w, str, len);
}

template <class S>
void renderPiece(const ListState* s, S& w) {
    switch (s->piece) {
        case ListState::HeaderPiece:
            put(w, kXmlDecl);
            put(w, "<ListBucketResult");
            put(w, kXmlNs);
            put(w, "><Name>");
            put(w, s->bucket->name);
            put(w, "</Name><Prefix>");
            putText(s, w, s->prefix, s->prefixLen);
            put(w, "</Prefix>");
            if (!s->v2) {
                put(w, "<Marker>");
                if (s->markerFrom == ListState::V1Marker) putText(s, w, s->marker, strlen(s->marker));
                put(w, "</Marker>");
            }
            put(w, "<MaxKeys>");
            s3::putUint(w, s->maxKeys);
            put(w, "</MaxKeys>");
            if (s->delimiter) put(w, "<Delimiter>/</Delimiter>");
            if (s->urlEncode) put(w, "<EncodingType>url</EncodingType>");
            if (s->markerFrom == ListState::Token) {
                put(w, "<ContinuationToken>");
                s3::putHex(w, s->marker, strlen(s->marker));
                put(w, "</ContinuationToken>");
            } else if (s->markerFrom == ListState::StartAfter) {
                put(w, "<StartAfter>");
                putText(s, w, s->marker, strlen(s->marker));
                put(w, "</StartAfter>");
            }
            break;
        case ListState::ItemPiece: {
            const size_t len = strlen(s->marker);  // marker == the entry being emitted
            if (s->itemIsPrefix) {
                put(w, "<CommonPrefixes><Prefix>");
                putText(s, w, s->marker, len);
                put(w, "</Prefix></CommonPrefixes>");
                break;
            }
            put(w, "<Contents><Key>");
            putText(s, w, s->marker, len);
            put(w, "</Key><LastModified>");
            char iso[25];
            s3::formatIso8601(s->itemMtime, iso);
            put(w, iso);
            put(w, "</LastModified><ETag>");
            char etag[40];
            s3::formatETag(s->itemSize, static_cast<uint64_t>(s->itemMtime), etag);
            s3::putXml(w, etag);
            put(w, "</ETag><Size>");
            s3::putUint(w, s->itemSize);
            put(w, "</Size><StorageClass>STANDARD</StorageClass></Contents>");
            break;
        }
        case ListState::FooterPiece:
            if (s->v2) {
                put(w, "<KeyCount>");
                s3::putUint(w, s->emitted);
                put(w, "</KeyCount>");
            }
            put(w, s->truncated ? "<IsTruncated>true</IsTruncated>" : "<IsTruncated>false</IsTruncated>");
            if (s->truncated && s->hasMarker) {
                if (s->v2) {
                    put(w, "<NextContinuationToken>");
                    s3::putHex(w, s->marker, strlen(s->marker));
                    put(w, "</NextContinuationToken>");
                } else {
                    put(w, "<NextMarker>");
                    putText(s, w, s->marker, strlen(s->marker));
                    put(w, "</NextMarker>");
                }
            }
            put(w, "</ListBucketResult>");
            break;
        case ListState::None:
            break;
    }
}

// Picks the next piece; false when the search budget for this fill is spent.
bool prepareNext(ListState* s, int& budget, bool haveOutput) {
    for (;;) {
        switch (s->phase) {
            case ListState::Header:
                s->piece = ListState::HeaderPiece;
                return true;
            case ListState::Footer:
                s->piece = ListState::FooterPiece;
                return true;
            case ListState::Done:
                return false;
            case ListState::Items:
                break;
        }
        if (budget <= 0 && haveOutput) return false;
        --budget;
        const bool found = findNext(s);
        if (s->emitted >= s->maxKeys) {
            s->truncated = found;
            s->phase = ListState::Footer;
            continue;
        }
        if (!found) {
            s->phase = ListState::Footer;
            continue;
        }
        memcpy(s->marker, s->best, sizeof(s->marker));
        s->hasMarker = true;
        s->itemIsPrefix = s->bestIsPrefix;
        if (!s->itemIsPrefix) {
            const size_t len = strlen(s->marker);
            memcpy(s->rel(), s->marker, len + 1);
            struct stat st{};
            if (stat(s->path, &st) != 0) continue;  // vanished meanwhile: skip it
            s->itemSize = static_cast<uint32_t>(st.st_size);
            s->itemMtime = static_cast<int64_t>(st.st_mtime);
        }
        ++s->emitted;
        s->piece = ListState::ItemPiece;
        return true;
    }
}

size_t listFill(AsyncWebServerRequest* request, uint8_t* buffer, size_t maxLen) {
    auto* s = static_cast<ListState*>(request->_tempObject);
    if (!s || s->kind != Kind::List) return 0;
    size_t out = 0;
    int budget = kSearchesPerFill;
    while (out < maxLen) {
        if (s->piece == ListState::None && !prepareNext(s, budget, out > 0)) break;
        WindowOutputStream w(buffer + out, s->pieceOff, maxLen - out);
        renderPiece(s, w);
        out += w.copied();
        if (s->pieceOff + w.copied() < w.total()) {
            s->pieceOff += static_cast<uint32_t>(w.copied());
            break;  // buffer full mid-piece
        }
        s->pieceOff = 0;
        const auto done = s->piece;
        s->piece = ListState::None;
        if (done == ListState::HeaderPiece) s->phase = ListState::Items;
        if (done == ListState::FooterPiece) {
            s->phase = ListState::Done;
            ESP_LOGI(TAG, "list: %u entries, %u walks, %lld ms", s->emitted, s->searches,
                     (esp_timer_get_time() - s->startedUs) / 1000);
        }
    }
    return out;
}

void handleList(AsyncWebServerRequest* request, const s3::Bucket& bucket) {
    const String* listType = paramValue(request, "list-type");
    const bool v2 = listType && *listType == "2";
    const String* prefix = paramValue(request, "prefix");
    const String* delim = paramValue(request, "delimiter");
    const String* maxKeys = paramValue(request, "max-keys");
    const String* enc = paramValue(request, "encoding-type");
    const String* token = v2 ? paramValue(request, "continuation-token") : nullptr;
    const String* startAfter = v2 ? paramValue(request, "start-after") : paramValue(request, "marker");
    if (token && !token->length()) token = nullptr;
    if (startAfter && !startAfter->length()) startAfter = nullptr;

    if (delim && delim->length() && *delim != "/") {
        sendXmlError(request, 400, "InvalidArgument", "only the '/' delimiter is supported");
        return;
    }
    if (enc && enc->length() && *enc != "url") {
        sendXmlError(request, 400, "InvalidArgument", "invalid encoding-type");
        return;
    }
    if (!token && startAfter && startAfter->length() > s3::kReadKeyMax) {
        sendXmlError(request, 400, "InvalidArgument", "marker too long");
        return;
    }

    ListState* s = allocState<ListState>(request);
    if (!s) {
        sendXmlError(request, 503, "SlowDown", "out of memory");
        return;
    }
    s->startedUs = esp_timer_get_time();
    s->bucket = &bucket;
    s->v2 = v2;
    s->delimiter = delim && *delim == "/";
    s->urlEncode = enc && *enc == "url";
    char mount[kMountMax + 1];
    const int mountLen = mountPoint(mount);
    if (mountLen < 0) {
        sendXmlError(request, 503, "ServiceUnavailable", "filesystem not mounted");
        return;
    }
    s->baseLen = static_cast<uint8_t>(
        s3::bucketBase(bucket, mount, static_cast<size_t>(mountLen), s->path, sizeof(s->path)));
    if (prefix) {
        if (prefix->length() > s3::kReadKeyMax) {
            s->noMatch = true;  // no key can be that long
        } else {
            s->prefixLen = static_cast<uint8_t>(prefix->length());
            memcpy(s->prefix, prefix->c_str(), s->prefixLen + 1);
        }
    }
    if (maxKeys) {
        const long m = strtol(maxKeys->c_str(), nullptr, 10);
        s->maxKeys = static_cast<uint16_t>(m < 0 ? 0 : (m > 1000 ? 1000 : m));
    }
    if (token && token->length()) {
        if (s3::hexDecode(token->c_str(), token->length(), s->marker, sizeof(s->marker)) < 0) {
            sendXmlError(request, 400, "InvalidArgument", "invalid continuation token");
            return;
        }
        s->hasMarker = true;
        s->markerFrom = ListState::Token;
    } else if (startAfter && startAfter->length()) {
        memcpy(s->marker, startAfter->c_str(), startAfter->length() + 1);
        s->hasMarker = true;
        s->markerFrom = v2 ? ListState::StartAfter : ListState::V1Marker;
    }

    AsyncWebServerResponse* response = request->beginChunkedResponse(
        "application/xml", [request](uint8_t* buffer, size_t maxLen, size_t) -> size_t {
            return listFill(request, buffer, maxLen);
        });
    addCommonHeaders(response);
    request->send(response);
}

// ---------------------------------------------------------------------------
// Objects: GET / HEAD / DELETE
// ---------------------------------------------------------------------------

void handleGetObject(AsyncWebServerRequest* request, const Target& t) {
    if (!s3::keyWellFormed(t.key, t.keyLen, s3::kReadKeyMax)) {
        sendXmlError(request, 400, "InvalidArgument", "invalid key");
        return;
    }
    if (!s3::keyReadable(*t.bucket, t.key, t.keyLen)) {  // in another bucket's directory
        sendXmlError(request, 404, "NoSuchKey", "The specified key does not exist.");
        return;
    }
    char path[kMountMax + 1 + s3::kDirMax + 1 + s3::kReadKeyMax + 1];
    if (!buildPath(*t.bucket, path, sizeof path, t.key, t.keyLen)) {
        sendXmlError(request, 503, "ServiceUnavailable", "filesystem not mounted");
        return;
    }
    struct stat st{};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        sendXmlError(request, 404, "NoSuchKey", "The specified key does not exist.");
        return;
    }
    const char* type = s3::contentTypeFor(t.key, t.keyLen);
    const auto size = static_cast<size_t>(st.st_size);

    AsyncWebServerResponse* response;
    if (request->method() == HTTP_HEAD || size == 0) {
        response = request->beginResponse(200, type, "");
        response->setContentLength(size);
    } else {
        if (s_getsActive >= kMaxGets) {
            sendXmlError(request, 503, "SlowDown", "too many concurrent downloads");
            return;
        }
        const int fd = open(path, O_RDONLY);
        if (fd < 0) {
            sendXmlError(request, 500, "InternalError", "open failed");
            return;
        }
        GetState* g = allocState<GetState>(request);
        if (!g) {
            close(fd);
            sendXmlError(request, 503, "SlowDown", "out of memory");
            return;
        }
        g->fd = fd;
        ++s_getsActive;
        watchState(request);
        response = request->beginResponse(type, size, [request](uint8_t* buffer, size_t maxLen, size_t) -> size_t {
            auto* gs = static_cast<GetState*>(request->_tempObject);
            if (!gs || gs->kind != Kind::Get || gs->fd < 0) return 0;
            const ssize_t n = read(gs->fd, buffer, maxLen);
            return n > 0 ? static_cast<size_t>(n) : 0;
        });
    }
    formatStatHeaders(response, st);
    addCommonHeaders(response);
    request->send(response);
}

constexpr const char* kKeyRulesMsg =
    "key must be [A-Za-z0-9._-/], no '..' or empty segments, and end in .js, .json or .txt";

// Status, code and message of a refused write key (status 0: allowed).
struct WriteRefusal {
    uint16_t status;
    const char* code;
    const char* msg;
};

WriteRefusal checkWrite(const Target& t) {
    switch (s3::checkWriteKey(*t.bucket, t.key, t.keyLen)) {
        case s3::KeyCheck::Ok:
            return {0, nullptr, nullptr};
        case s3::KeyCheck::ReadOnly:
            return {403, "AccessDenied", "bucket is read-only"};
        case s3::KeyCheck::Forbidden:
            return {403, "AccessDenied", "this path is read-only"};
        case s3::KeyCheck::OtherBucket:
            return {400, "InvalidArgument", "key lies in another bucket's directory (see ListBuckets)"};
        case s3::KeyCheck::Invalid:
            break;
    }
    if (t.keyLen > s3::kWriteKeyMax) return {400, "KeyTooLongError", "key longer than 63 characters"};
    return {400, "InvalidArgument", kKeyRulesMsg};
}

bool writeKeyAllowed(AsyncWebServerRequest* request, const Target& t) {
    const WriteRefusal r = checkWrite(t);
    if (r.status) sendXmlError(request, r.status, r.code, r.msg);
    return r.status == 0;
}

void handleDeleteObject(AsyncWebServerRequest* request, const Target& t) {
    if (!writeKeyAllowed(request, t)) return;
    if (s3::keyIsConfig(*t.bucket, t.key, t.keyLen)) {
        sendXmlError(request, 403, "AccessDenied", "config.json cannot be deleted");
        return;
    }
    char path[kMountMax + 1 + s3::kWriteKeyMax + 1];
    if (!buildPath(*t.bucket, path, sizeof path, t.key, t.keyLen)) {
        sendXmlError(request, 503, "ServiceUnavailable", "filesystem not mounted");
        return;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        if (errno == EBUSY) {
            sendXmlError(request, 503, "SlowDown", "file is in use, retry");
        } else {
            sendXmlError(request, 500, "InternalError", "delete failed");
        }
        return;
    }
    AsyncWebServerResponse* response = request->beginResponse(204);
    addCommonHeaders(response);
    request->send(response);
}

// ---------------------------------------------------------------------------
// PutObject
// ---------------------------------------------------------------------------

void mkdirParents(char* path, size_t mountLen) {
    for (char* p = path + mountLen + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(path, 0775);  // EEXIST is fine
        *p = '/';
    }
}

void putSink(PutState* st, const uint8_t* data, size_t len) {
    if (st->status || len == 0) return;
    if (st->written + len > s3::kMaxObjectSize) {
        st->fail(413, "EntityTooLarge", "object larger than 64 KiB");
        return;
    }
    if (st->lengthKnown && st->written + len > st->expected) {
        st->fail(400, "InvalidRequest", "body longer than the declared length");
        return;
    }
    st->crc = esp_rom_crc32_le(st->crc, data, static_cast<uint32_t>(len));
    if (st->haveMd5) mbedtls_md5_update(&st->md5, data, len);
    st->written += static_cast<uint32_t>(len);
    while (len) {
        const ssize_t n = write(st->fd, data, len);
        if (n <= 0) {
            st->fail(500, "InternalError", errno == ENOSPC ? "filesystem full" : "write failed");
            return;
        }
        data += n;
        len -= static_cast<size_t>(n);
    }
}

// First body byte (or end of an empty-body PUT): validate and open the temp file.
PutState* putBegin(AsyncWebServerRequest* request) {
    PutState* st = allocState<PutState>(request);
    if (!st) return nullptr;
    watchState(request);

    const Target t = parseTarget(request->url());
    if (const WriteRefusal r = checkWrite(t); r.status) {
        st->fail(r.status, r.code, r.msg);
        return st;
    }
    if (request->hasParam("uploads") || request->hasParam("uploadId") || request->hasHeader("x-amz-copy-source")) {
        st->fail(501, "NotImplemented", "multipart upload and copy are not supported");
        return st;
    }
    st->isConfig = s3::keyIsConfig(*t.bucket, t.key, t.keyLen);

    const char* encoding = headerValue(request, "Content-Encoding");
    const char* sha = headerValue(request, "x-amz-content-sha256");
    st->chunked = (encoding && strstr(encoding, "aws-chunked")) || (sha && strncmp(sha, "STREAMING-", 10) == 0);
    if (const char* decoded = headerValue(request, "x-amz-decoded-content-length")) {
        st->expected = static_cast<uint32_t>(strtoul(decoded, nullptr, 10));
        st->lengthKnown = true;
    } else if (!st->chunked && request->hasHeader("Content-Length")) {
        st->expected = static_cast<uint32_t>(request->contentLength());
        st->lengthKnown = true;
    } else if (!request->hasHeader("Content-Length")) {
        st->fail(411, "MissingContentLength", "Content-Length or x-amz-decoded-content-length required");
        return st;
    }
    if (st->lengthKnown && st->expected > s3::kMaxObjectSize) {
        st->fail(413, "EntityTooLarge", "object larger than 64 KiB");
        return st;
    }
    if (const char* md5 = headerValue(request, "Content-MD5")) {
        if (s3::base64Decode(md5, strlen(md5), st->wantMd5, sizeof st->wantMd5) != 16) {
            st->fail(400, "InvalidDigest", "Content-MD5 is not a base64 MD5");
            return st;
        }
        mbedtls_md5_init(&st->md5);
        mbedtls_md5_starts(&st->md5);
        st->haveMd5 = true;
    }
    if (const char* crc = headerValue(request, "x-amz-checksum-crc32")) {
        if (!s3::parseCrc32B64(crc, strlen(crc), &st->wantCrc)) {
            st->fail(400, "InvalidRequest", "invalid x-amz-checksum-crc32");
            return st;
        }
        st->haveCrc = true;
    }
    if (s_putActive) {
        st->fail(503, "SlowDown", "another upload is in progress, retry");
        return st;
    }
    const size_t finalLen = buildPath(*t.bucket, st->path, sizeof st->path, t.key, t.keyLen, s3::kTmpSuffix);
    if (!finalLen) {
        st->path[0] = '\0';
        st->fail(503, "ServiceUnavailable", "filesystem not mounted");
        return st;
    }
    s_putActive = true;
    st->ownsSlot = true;
    st->finalLen = static_cast<uint8_t>(finalLen - s3::kTmpSuffixLen);
    mkdirParents(st->path, strlen(LittleFS.mountpoint()));
    st->fd = open(st->path, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (st->fd < 0) {
        st->fail(500, "InternalError", "cannot create file");
    }
    return st;
}

void putBody(AsyncWebServerRequest* request, const uint8_t* data, size_t len) {
    auto* st = static_cast<PutState*>(request->_tempObject);
    if (!st) {
        st = putBegin(request);
        if (!st) return;
    }
    if (st->kind != Kind::Put || st->status || st->fd < 0) return;
    if (!st->chunked) {
        putSink(st, data, len);
        return;
    }
    if (!st->dec.feed(data, len, [st](const uint8_t* p, size_t n) { putSink(st, p, n); })) {
        st->fail(400, "InvalidRequest", "malformed aws-chunked body");
    }
}

class FdReadStream {
public:
    typedef char Ch;
    explicit FdReadStream(int fd) : fd_(fd) { fill(); }
    Ch Peek() const { return pos_ < len_ ? buf_[pos_] : '\0'; }
    Ch Take() {
        if (pos_ >= len_) return '\0';
        const Ch c = buf_[pos_++];
        if (pos_ >= len_) fill();
        return c;
    }
    size_t Tell() const { return consumed_ + pos_; }

private:
    void fill() {
        consumed_ += len_;
        const ssize_t n = read(fd_, buf_, sizeof buf_);
        len_ = n > 0 ? static_cast<size_t>(n) : 0;
        pos_ = 0;
    }
    int fd_;
    size_t consumed_ = 0;
    size_t len_ = 0;
    size_t pos_ = 0;
    char buf_[128];
};

void putFinish(AsyncWebServerRequest* request) {
    auto* st = static_cast<PutState*>(request->_tempObject);
    if (!st) st = putBegin(request);  // empty body: no handleBody call
    if (!st || st->kind != Kind::Put) {
        sendXmlError(request, 503, "SlowDown", "out of memory");
        return;
    }
    if (request->multipart()) st->fail(400, "InvalidRequest", "multipart/form-data bodies are not supported");
    if (!st->status && st->chunked && !st->dec.done()) st->fail(400, "IncompleteBody", "aws-chunked body ended early");
    if (!st->status && st->lengthKnown && st->written != st->expected) {
        st->fail(400, "IncompleteBody", "body shorter than the declared length");
    }
    if (st->fd >= 0) {
        if (close(st->fd) != 0) st->fail(500, "InternalError", "write failed");
        st->fd = -1;
    }
    if (!st->status && st->haveMd5) {
        uint8_t digest[16];
        mbedtls_md5_finish(&st->md5, digest);
        if (memcmp(digest, st->wantMd5, sizeof digest) != 0) {
            st->fail(400, "BadDigest", "Content-MD5 does not match the body");
        }
    }
    if (!st->status && st->chunked && st->dec.hasCrc32()) {
        uint32_t trailerCrc = 0;
        if (!s3::parseCrc32B64(st->dec.crc32B64(), strlen(st->dec.crc32B64()), &trailerCrc)) {
            st->fail(400, "InvalidRequest", "invalid x-amz-checksum-crc32 trailer");
        } else if (trailerCrc != st->crc) {
            st->fail(400, "BadDigest", "x-amz-checksum-crc32 does not match the body");
        }
        st->haveCrc = true;
        st->wantCrc = trailerCrc;
    }
    if (!st->status && st->haveCrc && st->wantCrc != st->crc) {
        st->fail(400, "BadDigest", "x-amz-checksum-crc32 does not match the body");
    }
    if (!st->status && st->isConfig) {
        const int fd = open(st->path, O_RDONLY);
        if (fd < 0) {
            st->fail(500, "InternalError", "cannot read back the upload");
        } else {
            FdReadStream in(fd);
            size_t offset = 0;
            const bool ok = json::validateJson(in, &offset);
            close(fd);
            if (!ok) {
                st->jsonOffset = static_cast<uint32_t>(offset);
                st->fail(400, "InvalidArgument", nullptr);
            }
        }
    }

    char finalPath[sizeof st->path];
    if (!st->status) {
        memcpy(finalPath, st->path, st->finalLen);
        finalPath[st->finalLen] = '\0';
        if (rename(st->path, finalPath) != 0) {
            if (errno == EBUSY) st->fail(503, "SlowDown", "file is in use, retry");
            else st->fail(500, "InternalError", "rename failed");
        } else {
            st->committed = true;
        }
    }

    if (st->status) {
        char msg[48];
        const char* text = st->errMsg;
        if (!text) {
            snprintf(msg, sizeof msg, "invalid JSON at offset %lu", static_cast<unsigned long>(st->jsonOffset));
            text = msg;
        }
        ESP_LOGW(TAG, "PUT %s: %u %s %s", request->url().c_str(), st->status, st->errCode, text);
        const uint16_t status = st->status;
        const char* code = st->errCode;
        releaseState(request);  // drop the temp file before answering
        sendXmlError(request, status, code, text);
        return;
    }

    struct stat fst{};
    stat(finalPath, &fst);
    ESP_LOGI(TAG, "PUT %s: %lu bytes", request->url().c_str(), static_cast<unsigned long>(st->written));
    AsyncWebServerResponse* response = request->beginResponse(200);
    char etag[40];
    s3::formatETag(st->written, static_cast<uint64_t>(fst.st_mtime), etag);
    response->addHeader("ETag", etag);
    if (st->haveCrc) {
        char crc[9];
        s3::formatCrc32B64(st->crc, crc);
        response->addHeader("x-amz-checksum-crc32", crc);
    }
    if (st->isConfig) response->addHeader("x-freebutton-reboot-required", "true");
    addCommonHeaders(response);
    releaseState(request);
    request->send(response);
}

// ---------------------------------------------------------------------------
// Handler
// ---------------------------------------------------------------------------

void handlePreflight(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(200);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, PUT, HEAD, DELETE");
    const char* requested = headerValue(request, "Access-Control-Request-Headers");
    response->addHeader("Access-Control-Allow-Headers", requested && *requested ? requested : "*");
    response->addHeader("Access-Control-Expose-Headers", kExposeHeaders);
    response->addHeader("Access-Control-Max-Age", "600");
    request->send(response);
}

class S3Handler final : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        const Target t = parseTarget(request->url());
        if (t.type == Target::BucketRoot || t.type == Target::Object) {
            // /fonts/... is also an IDE URL: browsers' GET/HEAD go to serveStatic.
            const auto method = request->method();
            if (t.bucket->sharesStaticPath && (method == HTTP_GET || method == HTTP_HEAD) &&
                !isS3Request(request)) {
                return false;
            }
            // A PUT body sent as text/plain (the SDK's .txt / "text/plain;
            // charset=utf-8") or form-urlencoded would be parsed by the server
            // as form fields instead of reaching handleBody. Relabel it before
            // the body arrives; the content type is not stored anyway.
            if (method == HTTP_PUT) {
                const String& ct = request->contentType();
                if (ct.equalsIgnoreCase("text/plain") ||
                    ct.startsWith("application/x-www-form-urlencoded")) {
                    const_cast<String&>(ct) = "application/octet-stream";
                }
            }
            return true;
        }
        if (t.type == Target::Root) {
            return request->method() == HTTP_GET && (request->hasParam("x-id") || isS3Client(request));
        }
        // Path-style requests for another bucket from an S3 client.
        const String& url = request->url();
        return !url.startsWith("/api") && !url.startsWith("/ws") && isS3Client(request);
    }

    void handleRequest(AsyncWebServerRequest* request) override {
        const Target t = parseTarget(request->url());
        const auto method = request->method();
        if (method == HTTP_OPTIONS && t.type != Target::Root) {
            handlePreflight(request);
            return;
        }
        switch (t.type) {
            case Target::Root:
                sendConstXml(request, s3::kListBucketsXml, sizeof(s3::kListBucketsXml) - 1);
                return;
            case Target::Unknown:
                sendXmlError(request, 404, "NoSuchBucket", "The specified bucket does not exist.");
                return;
            case Target::BucketRoot:
                if (method == HTTP_HEAD) {
                    AsyncWebServerResponse* response = request->beginResponse(200);
                    response->addHeader("x-amz-bucket-region", "us-east-1");
                    addCommonHeaders(response);
                    request->send(response);
                } else if (method == HTTP_GET && request->hasParam("location")) {
                    sendConstXml(request, kLocationXml, sizeof(kLocationXml) - 1);
                } else if (method == HTTP_GET && !request->hasParam("uploads")) {
                    handleList(request, *t.bucket);
                } else {
                    sendXmlError(request, 501, "NotImplemented", "operation not supported");
                }
                return;
            case Target::Object:
                break;
        }
        if (request->hasParam("uploads") || request->hasParam("uploadId")) {
            if (method == HTTP_PUT) putFinish(request);  // answers 501 and cleans up
            else sendXmlError(request, 501, "NotImplemented", "multipart upload is not supported");
            return;
        }
        switch (method) {
            case HTTP_GET:
            case HTTP_HEAD:
                handleGetObject(request, t);
                return;
            case HTTP_PUT:
                putFinish(request);
                return;
            case HTTP_DELETE:
                handleDeleteObject(request, t);
                return;
            default:
                sendXmlError(request, 501, "NotImplemented", "operation not supported");
                return;
        }
    }

    void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) override {
        if (request->method() != HTTP_PUT || parseTarget(request->url()).type != Target::Object) return;
        putBody(request, data, len);
    }

    bool isRequestHandlerTrivial() const override { return false; }
};

}  // namespace

bool s3_idle() {
    return !s_putActive && s_getsActive == 0;
}

void s3_register(AsyncWebServer& server) {
    server.addHandler(new S3Handler());
}
