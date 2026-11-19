//
// S3Server - minimal S3-compatible API over LittleFS and the media store.
//
// Path-style addressing, one bucket per kind of content (table: s3::kBuckets
// in S3Logic.h), a directory on the LittleFS partition or a bucket of the
// media store (src/media/media_store.h, docs/MEDIA-STORE.md):
//
//   app       LittleFS /, minus the other buckets' dirs   read/write
//   web       LittleFS /www (the IDE bundle, gzipped)     read-only
//   examples  LittleFS /scripts/examples                  read-only
//   fonts     media store MEDIA_BUCKET_FONTS (TrueType)   read/write
//   images    media store MEDIA_BUCKET_IMAGES (LVGL .bin) read/write
//
// Object key = path relative to the bucket's directory ("config.json",
// "scripts/main.js" in app; "index.html.gz" in web; "button.js" in examples).
// Every LittleFS object is reachable through exactly one bucket: app does not
// list www/ or scripts/examples/ (the walk skips them without opening them),
// answers 404 NoSuchKey for reads and 400 InvalidArgument for writes of such
// keys. A media bucket is a flat key space of its own (no LittleFS paths).
//
//   GET  /                      ListBuckets (only for S3 clients: Authorization
//                               AWS*, any x-amz-* header, or ?x-id=...; browsers
//                               keep getting the web IDE)
//   HEAD /<b>[/]                HeadBucket (media buckets: + usage headers)
//   GET  /<b>[/]?list-type=2    ListObjectsV2 (prefix, delimiter "/", max-keys,
//                               continuation-token, start-after, encoding-type)
//   GET  /<b>[/]                ListObjects v1 (marker)
//   GET  /<b>[/]?location       GetBucketLocation
//   GET|HEAD /<b>/<key>         GetObject / HeadObject (Range ignored)
//   PUT  /<b>/<key>             PutObject (plain or aws-chunked body; app, fonts
//                               and images, read-only buckets answer 403
//                               AccessDenied)
//   DELETE /<b>/<key>           DeleteObject (as PUT; in app a key of any
//                               extension except .tmp)
//   OPTIONS /<b>[/...]          405 MethodNotAllowed. A CORS preflight does not
//                               get here: the server's CORS middleware
//                               (Auth.cpp) answers every cross-origin OPTIONS.
//
// /fonts/... is also the URL of the IDE's own web fonts (www/fonts/*.woff2):
// GET/HEAD there reach bucket fonts only from S3 clients (see isS3Request);
// a browser gets the IDE file. PUT/DELETE always reach the bucket. Unknown
// buckets: 404 NoSuchBucket (S3 clients).
//
// Authentication (Auth.h): reads of web, examples, fonts and images are
// anonymous; everything else, every write included, is SigV4-signed.
//
// Buckets fonts and images (media store):
//   keys      flat names of [A-Za-z0-9._-], at most MEDIA_KEY_MAX (96), ending
//             in .ttf or .otf (fonts) or .bin (images; else 400
//             InvalidArgument / KeyTooLongError). The store does not check
//             the content; the UI validates a raster when it maps it.
//   PUT       at most 2 MiB (413 EntityTooLarge). The size must be known up
//             front: Content-Length, or x-amz-decoded-content-length for
//             aws-chunked bodies (else 411 MissingContentLength), because
//             media_put_begin() reserves exactly that before any body byte is
//             written. 507 InsufficientStorage (message: needed / free /
//             reclaimable bytes) when it does not fit; 503 SlowDown when the
//             store is busy; 503 ServiceUnavailable without a store. The
//             decoded body streams into the store (chunk signatures, SHA-256,
//             Content-MD5 and CRC32 checked as for app); the commit is atomic
//             and stores the body's MD5. Any failure, or a client that
//             disconnects, aborts the upload: the old version stays.
//   GET/HEAD  the TCP stack copies the body from the memory-mapped flash (no
//             buffer of ours in between: StreamLogic.h); ETag = the stored
//             MD5 ("<32 hex>"), Last-Modified = upload time (1970 when the
//             clock was not set), Content-Type font/ttf, font/otf or
//             application/octet-stream.
//   DELETE    204, also for a missing key.
//   After a committed PUT or a DELETE: fonts reload (fonts_request_reload),
//             items showing images re-resolve (mediaImageNotifyChanged).
//   HeadBucket and listings also carry (decimal bytes, media_usage()):
//             x-fb-media-total        data area of the media partition
//             x-fb-media-used         allocated to live entries
//             x-fb-media-free         free now
//             x-fb-media-reclaimable  deleted/replaced, freed once closed
//           and of a LittleFS bucket (the filesystem app, web and examples
//           share; LittleFS walks it to count the used blocks):
//             x-fb-fs-total           size of the filesystem
//             x-fb-fs-used            used blocks
//           and wear since boot (counted in 4 KB sectors; a page erase is 16):
//             x-fb-media-erase-sectors   sectors erased
//             x-fb-media-erase-page-max  most sector erases on one 64 KB page
//             x-fb-media-dir-generation  directory snapshots since formatting
//
// Memory: per request one small fixed struct in request->_tempObject
// (malloc'd; the server free()s it), nothing that grows with the number of
// files or the object size. Listings are generated on the fly into the
// chunked response buffer; object bodies go straight between the TCP buffers
// and LittleFS or the media store. All handlers run on the async_tcp task,
// so the slot counters below need no locking.
//

// RapidJSON's default assert() embeds the full template signature of every
// parse function as a string (~400 B each, ~4 KB for the validator's
// instantiations). Same check, no strings.
#ifndef RAPIDJSON_ASSERT
#include <cstdlib>
#define RAPIDJSON_ASSERT(x) ((x) ? (void)0 : abort())
#endif

#include "S3Server.h"

#include "Auth.h"
#include "S3Logic.h"
#include "StreamResponse.h"
#include "json/JsonValidator.h"
#include "json/WindowOutputStream.h"
#include "ui/MediaImage.h"   // mediaImageNotifyChanged
#include "ui/fonts/fonts.h"  // fonts_request_reload

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
#include <time.h>
#include <unistd.h>

static auto TAG = "s3";

namespace {

using s3::put;

constexpr const char* kXmlDecl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
constexpr const char* kXmlNs = " xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"";
constexpr size_t kMountMax = 15;   // ESP_VFS_PATH_MAX
constexpr int kMaxDepth = 6;       // directory levels walked by a listing
constexpr int kSearchesPerFill = 4;
// Entries one walk of a LittleFS bucket collects (ListState::batch: 97 bytes each).
constexpr int kListBatch = 12;

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

enum class Kind : uint8_t { Put, List };

bool s_putActive = false;

// ---------------------------------------------------------------------------
// Per-request state (lives in request->_tempObject)
// ---------------------------------------------------------------------------

struct PutState {
    Kind kind = Kind::Put;
    bool chunked = false;
    bool lengthKnown = false;
    bool haveMd5 = false;  // Content-MD5 given: verify it
    bool md5Live = false;  // md5 needs mbedtls_md5_free (Content-MD5 or media bucket)
    bool haveCrc = false;
    bool isConfig = false;
    bool ownsSlot = false;
    bool committed = false;
    bool authFailed = false;  // the error is an authentication refusal
    bool noSpace = false;     // the error is MEDIA_ERR_NO_SPACE: message with the sizes
    bool shaLive = true;      // sha needs mbedtls_sha256_free (the memory is free()d, no destructor)
    fbauth::Payload payload = fbauth::Payload::Unsigned;
    uint16_t status = 0;  // != 0: request failed, respond with this
    const char* errCode = nullptr;
    const char* errMsg = nullptr;
    const char* tooLargeMsg = nullptr;
    int fd = -1;
    media_writer_t* writer = nullptr;  // media store upload in progress
    uint32_t limit = 0;                // largest body for the bucket
    uint32_t expected = 0;
    uint32_t written = 0;
    uint32_t crc = 0;
    uint32_t wantCrc = 0;
    uint32_t jsonOffset = 0;
    uint8_t wantMd5[16] = {};
    uint8_t finalLen = 0;  // path length without ".tmp"
    s3::AwsChunkedDecoder dec;
    mbedtls_md5_context md5;
    fbauth::Sha256 sha;          // whole body (payload Sha256) or the current chunk (Streaming)
    fbauth::ChunkVerifier chunks;  // Streaming: chunk signature chain
    uint8_t wantSha[32] = {};    // payload Sha256: the signed body hash
    char path[kMountMax + 1 + s3::kWriteKeyMax + s3::kTmpSuffixLen + 1] = {};

    void fail(uint16_t s, const char* code, const char* msg) {
        if (status) return;  // keep the first error
        status = s;
        errCode = code;
        errMsg = msg;
    }
    void fail(const s3::HttpError& e) { fail(e.status, e.code, e.msg); }
    void failMedia(media_err_t e) {
        if (!status && e == MEDIA_ERR_NO_SPACE) noSpace = true;
        fail(s3::mediaError(e, s3::MediaOp::Write));
    }
};

// Releases the media store reservation of an upload that will not be
// committed; idempotent (the pointer is cleared).
void dropWriter(PutState* p) {
    if (!p->writer) return;
    media_put_abort(p->writer);
    p->writer = nullptr;
}

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
    uint8_t itemMd5[16] = {};  // media buckets: the ETag
    int64_t startedUs = 0;
    const s3::Bucket* bucket = nullptr;
    char prefix[s3::kReadKeyMax + 1] = {};
    char marker[s3::kReadKeyMax + 1] = {};
    // The entry found (findNext). During a walk of a LittleFS bucket: the
    // upper bound, i.e. the largest entry of a full batch.
    char best[s3::kReadKeyMax + 1] = {};
    // LittleFS buckets: the next entries after the marker, in order, found
    // by one walk (kListBatch of them, so a listing takes a walk per
    // kListBatch entries and not one per entry: 68 files took 10 s).
    uint8_t batchCount = 0;
    uint8_t batchNext = 0;
    bool batchIsPrefix[kListBatch] = {};
    char batch[kListBatch][s3::kReadKeyMax + 1] = {};
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
    if (*kind == Kind::Put) {
        auto* p = reinterpret_cast<PutState*>(kind);
        if (p->fd >= 0) {
            close(p->fd);
            p->fd = -1;
        }
        dropWriter(p);  // not committed (a commit clears it)
        if (!p->committed && p->path[0]) {
            unlink(p->path);  // the .tmp file
            p->path[0] = '\0';
        }
        if (p->md5Live) {
            mbedtls_md5_free(&p->md5);
            p->md5Live = false;
        }
        if (p->shaLive) {
            p->sha.~Sha256();
            p->shaLive = false;
        }
        if (p->ownsSlot) {
            s_putActive = false;
            p->ownsSlot = false;
        }
    }
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

// CORS headers are not added here: a cross-origin request gets them from the
// auth middleware (Auth.cpp), a same-origin one (the IDE on the device) does
// not need their 300 bytes.

void sendXmlError(AsyncWebServerRequest* request, int status, const char* code, const char* message,
                  bool authDenied = false) {
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
    if (authDenied) fbauth::addDeniedHeaders(response);
    request->send(response);
}

void sendHttpError(AsyncWebServerRequest* request, const s3::HttpError& e) {
    sendXmlError(request, e.status, e.code, e.msg);
}

// A header with a decimal value.
void addUintHeader(AsyncWebServerResponse* response, const char* name, uint32_t value) {
    char num[11];
    snprintf(num, sizeof num, "%lu", static_cast<unsigned long>(value));
    response->addHeader(name, num);
}

// Space of the bucket's store: the media partition, or the LittleFS
// filesystem (HeadBucket and listings; the IDE shows it under its file lists).
void addUsageHeaders(AsyncWebServerResponse* response, const s3::Bucket& bucket) {
    if (!bucket.isMedia()) {
        addUintHeader(response, s3::kFsTotalHeader, LittleFS.totalBytes());
        addUintHeader(response, s3::kFsUsedHeader, LittleFS.usedBytes());
        return;
    }
    media_usage_t u{};
    media_usage(&u);
    addUintHeader(response, s3::kMediaTotalHeader, u.total);
    addUintHeader(response, s3::kMediaUsedHeader, u.used);
    addUintHeader(response, s3::kMediaFreeHeader, u.free);
    addUintHeader(response, s3::kMediaReclaimableHeader, u.reclaimable);
    addUintHeader(response, s3::kMediaEraseSectorsHeader, u.erase_sectors);
    addUintHeader(response, s3::kMediaErasePageMaxHeader, u.erase_page_max);
    addUintHeader(response, s3::kMediaDirGenerationHeader, u.dir_generation);
}

// After a committed PUT or a DELETE in a media bucket: whatever shows the
// object follows.
void notifyMediaChanged(const s3::Bucket& bucket) {
    if (bucket.media == MEDIA_BUCKET_FONTS) {
        fonts_request_reload();  // the LVGL task reloads the fonts 1.5 s after the last change
    } else if (bucket.media == MEDIA_BUCKET_IMAGES) {
        freebutton::ui::mediaImageNotifyChanged();  // items showing the key resolve it again
    }
}

// MEDIA_OK / NOT_FOUND when the media bucket can be read (an empty bucket
// is fine), else the store's error (UNAVAILABLE without a partition).
media_err_t probeMediaBucket(const s3::Bucket& b) {
    media_stat_t first;
    const media_err_t e = media_next(b.mediaBucket(), nullptr, &first);
    return e == MEDIA_ERR_NOT_FOUND ? MEDIA_OK : e;
}

// The NUL-terminated media key (keyLen <= MEDIA_KEY_MAX checked by the caller).
void copyMediaKey(const s3::UrlTarget& t, char out[MEDIA_KEY_MAX + 1]) {
    const size_t n = t.keyLen > MEDIA_KEY_MAX ? MEDIA_KEY_MAX : t.keyLen;
    memcpy(out, t.key, n);
    out[n] = '\0';
}

// A signature that was used before: an S3 client that repeats a request
// within the same second signs it identically. 503 SlowDown makes it wait
// and sign again; someone replaying a captured request gains nothing by it.
bool isReplay(const fbauth::Result& r) { return r.status == fbauth::Status::RequestReplayed; }

void sendAuthError(AsyncWebServerRequest* request, const fbauth::Result& r) {
    if (isReplay(r)) {
        sendXmlError(request, 503, "SlowDown", r.message);
        return;
    }
    sendXmlError(request, 403, fbauth::statusCode(r.status), r.message, true);
}

// Signature check for requests without a body (everything but PutObject):
// the signed payload must be UNSIGNED-PAYLOAD, or the hash of "" with
// Content-Length 0 (AuthLogic.h bodylessPayload).
bool verifyNoBody(AsyncWebServerRequest* request, fbauth::Result& r) {
    if (!fbauth::verifyRequest(request, r)) return false;
    if (fbauth::bodylessPayload(r, request->contentLength()) == fbauth::Bodyless::Ok) return true;
    r.status = fbauth::Status::SignatureDoesNotMatch;
    r.message = "x-amz-content-sha256 does not match the (empty) body";
    return false;
}

void sendConstXml(AsyncWebServerRequest* request, const char* xml, size_t len) {
    AsyncWebServerResponse* response = stream::beginConst(xml, len, "application/xml");
    if (!response) {
        sendHttpError(request, s3::kOutOfMemory);
        return;
    }
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
    // Without this a browser may reuse a stored copy for a while (heuristic
    // freshness from Last-Modified) and show a file as it was before a PUT.
    response->addHeader("Cache-Control", "no-cache");
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

// Offers value[0..len) (rel prefix) as one of the next listing entries: the
// batch keeps the kListBatch smallest values above the marker, in order.
// Once it is full, its largest value is the bound (best) for the walk.
void consider(ListState* s, size_t len, bool isPrefix, size_t dirLenToCheck, int depth) {
    const char* value = s->rel();
    if (s->hasMarker && cmpN(value, len, s->marker) <= 0) return;
    if (s->haveBest && cmpN(value, len, s->best) >= 0) return;
    // Where it goes; a value already there (files rolled up to one prefix) is not new.
    int at = 0;
    while (at < s->batchCount) {
        const int c = cmpN(value, len, s->batch[at]);
        if (c == 0) return;
        if (c < 0) break;
        ++at;
    }
    if (dirLenToCheck && !dirHasFile(s, dirLenToCheck, depth + 1)) return;
    const int last = s->batchCount < kListBatch ? s->batchCount : kListBatch - 1;   // a full batch drops its largest
    for (int i = last; i > at; --i) {
        memcpy(s->batch[i], s->batch[i - 1], sizeof s->batch[i]);
        s->batchIsPrefix[i] = s->batchIsPrefix[i - 1];
    }
    memcpy(s->batch[at], value, len);  // dirHasFile left rel[0..dirLen) intact
    s->batch[at][len] = '\0';
    s->batchIsPrefix[at] = isPrefix;
    if (s->batchCount < kListBatch) ++s->batchCount;
    if (s->batchCount == kListBatch) {
        memcpy(s->best, s->batch[kListBatch - 1], sizeof s->best);
        s->haveBest = true;
    }
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

// Finds the smallest listing value > marker into best. LittleFS buckets:
// one walk finds the next kListBatch entries (pruned to the subtrees that
// can hold them); the calls after it take them from the batch. A file made
// or removed meanwhile shows from the next walk on.
// Media bucket: the store iterates in byte-wise key order, so the next value
// is the first key > marker that has the prefix, rolled up at the delimiter.
// best doubles as the iteration cursor; the item's size, mtime and MD5 are
// taken from the same record.
bool findNextMedia(ListState* s) {
    media_stat_t st;
    const char* after = s->hasMarker ? s->marker : nullptr;
    for (;;) {
        const media_err_t e = media_next(s->bucket->mediaBucket(), after, &st);
        if (e == MEDIA_ERR_NOT_FOUND) return false;
        if (e != MEDIA_OK) {
            ESP_LOGW(TAG, "list %s: media_next failed (%d), listing cut short", s->bucket->name, e);
            s->noMatch = true;
            return false;
        }
        const size_t len = strnlen(st.key, sizeof st.key - 1);
        memcpy(s->best, st.key, len);
        s->best[len] = '\0';
        after = s->best;
        const int c = strncmp(s->best, s->prefix, s->prefixLen);
        if (c < 0) continue;      // before the prefix range
        if (c > 0) return false;  // past it: nothing more can match
        bool isPrefix = false;
        const size_t rl = s3::rollup(s->best, len, s->prefixLen, s->delimiter, &isPrefix);
        if (s->hasMarker && cmpN(s->best, rl, s->marker) <= 0) continue;  // inside the listed common prefix
        s->best[rl] = '\0';
        s->bestIsPrefix = isPrefix;
        s->itemSize = st.size;
        s->itemMtime = st.mtime;
        memcpy(s->itemMd5, st.md5, sizeof s->itemMd5);
        return true;
    }
}

bool findNext(ListState* s) {
    s->haveBest = false;
    ++s->searches;
    if (s->noMatch) return false;
    if (s->bucket->isMedia()) return s->haveBest = findNextMedia(s);
    if (s->batchNext >= s->batchCount) {
        s->batchCount = 0;
        s->batchNext = 0;
        s->rel()[0] = '\0';
        scanDir(s, 0, 0);
        if (s->batchCount == 0) return s->haveBest = false;
    } else {
        --s->searches;  // from the batch: no walk
    }
    memcpy(s->best, s->batch[s->batchNext], sizeof s->best);
    s->bestIsPrefix = s->batchIsPrefix[s->batchNext];
    ++s->batchNext;
    return s->haveBest = true;
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
            if (s->bucket->isMedia()) s3::formatMd5ETag(s->itemMd5, etag);
            else s3::formatETag(s->itemSize, static_cast<uint64_t>(s->itemMtime), etag);
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
        const uint16_t walksBefore = s->searches;
        const bool found = findNext(s);
        if (s->searches != walksBefore) --budget;  // only a walk costs time
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
        if (!s->itemIsPrefix && !s->bucket->isMedia()) {  // media: findNext took the record
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
        sendHttpError(request, s3::kOutOfMemory);
        return;
    }
    s->startedUs = esp_timer_get_time();
    s->bucket = &bucket;
    s->v2 = v2;
    s->delimiter = delim && *delim == "/";
    s->urlEncode = enc && *enc == "url";
    if (bucket.isMedia()) {
        // Refuse up front: once the chunked response has started, an error
        // can only cut the listing short.
        if (const media_err_t e = probeMediaBucket(bucket); e != MEDIA_OK) {
            sendHttpError(request, s3::mediaError(e, s3::MediaOp::Read));
            return;
        }
    } else {
        char mount[kMountMax + 1];
        const int mountLen = mountPoint(mount);
        if (mountLen < 0) {
            sendHttpError(request, s3::kNotMounted);
            return;
        }
        s->baseLen = static_cast<uint8_t>(
            s3::bucketBase(bucket, mount, static_cast<size_t>(mountLen), s->path, sizeof(s->path)));
    }
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

    // Takes its turn with the downloads; the request's headers are dropped
    // while it waits and sends (everything it needs is in the state).
    AsyncWebServerResponse* response = stream::beginChunked(
        "application/xml",
        [](void* context, uint8_t* buffer, size_t maxLen, size_t) -> size_t {
            return listFill(static_cast<AsyncWebServerRequest*>(context), buffer, maxLen);
        },
        request);
    if (!response) {
        sendHttpError(request, s3::kOutOfMemory);
        return;
    }
    addUsageHeaders(response, bucket);
    request->send(response);
}

// ---------------------------------------------------------------------------
// Objects: GET / HEAD / DELETE
// ---------------------------------------------------------------------------

void addMediaStatHeaders(AsyncWebServerResponse* response, const uint8_t md5[16], uint32_t mtime) {
    char etag[35];
    s3::formatMd5ETag(md5, etag);
    response->addHeader("ETag", etag);
    char date[30];
    s3::formatHttpDate(static_cast<int64_t>(mtime), date);
    response->addHeader("Last-Modified", date);
}

// GetObject / HeadObject in a media bucket. The file is mapped when the
// download gets its turn and unmapped when the download ends; the TCP stack
// copies each segment from the mapping (StreamResponse.h, StreamLogic.h).
void handleMediaGet(AsyncWebServerRequest* request, const Target& t) {
    switch (s3::checkMediaReadKey(t.key, t.keyLen)) {
        case s3::MediaReadKey::Ok:
            break;
        case s3::MediaReadKey::Malformed:
            sendHttpError(request, s3::kInvalidKey);
            return;
        case s3::MediaReadKey::Missing:
            sendHttpError(request, s3::kNoSuchKey);
            return;
    }
    char key[MEDIA_KEY_MAX + 1];
    copyMediaKey(t, key);
    const media_bucket_t mb = t.bucket->mediaBucket();
    const char* type = s3::contentTypeFor(t.key, t.keyLen);

    media_stat_t st;
    if (const media_err_t e = media_stat(mb, key, &st); e != MEDIA_OK) {
        sendHttpError(request, s3::mediaError(e, s3::MediaOp::Read));
        return;
    }
    AsyncWebServerResponse* response;
    if (request->method() == HTTP_HEAD || st.size == 0) {
        response = request->beginResponse(200, type, "");
        response->setContentLength(st.size);
    } else {
        response = stream::beginMedia(mb, key, st, type);
        if (!response) {
            sendHttpError(request, s3::kOutOfMemory);
            return;
        }
    }
    addMediaStatHeaders(response, st.md5, st.mtime);
    request->send(response);
}

void handleGetObject(AsyncWebServerRequest* request, const Target& t) {
    if (t.bucket->isMedia()) {
        handleMediaGet(request, t);
        return;
    }
    if (!s3::keyWellFormed(t.key, t.keyLen, s3::kReadKeyMax)) {
        sendHttpError(request, s3::kInvalidKey);
        return;
    }
    if (!s3::keyReadable(*t.bucket, t.key, t.keyLen)) {  // in another bucket's directory
        sendHttpError(request, s3::kNoSuchKey);
        return;
    }
    char path[kMountMax + 1 + s3::kDirMax + 1 + s3::kReadKeyMax + 1];
    if (!buildPath(*t.bucket, path, sizeof path, t.key, t.keyLen)) {
        sendHttpError(request, s3::kNotMounted);
        return;
    }
    struct stat st{};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        sendHttpError(request, s3::kNoSuchKey);
        return;
    }
    const char* type = s3::contentTypeFor(t.key, t.keyLen);
    const auto size = static_cast<size_t>(st.st_size);

    AsyncWebServerResponse* response;
    if (request->method() == HTTP_HEAD || size == 0) {
        response = request->beginResponse(200, type, "");
        response->setContentLength(size);
    } else {
        // Opened when the download gets its turn (StreamResponse.h).
        response = stream::beginFile(path, size, type);
        if (!response) {
            sendHttpError(request, s3::kOutOfMemory);
            return;
        }
    }
    formatStatHeaders(response, st);
    request->send(response);
}

s3::HttpError checkWrite(const Target& t, bool forDelete = false) {
    return s3::writeRefusal(*t.bucket, t.key, t.keyLen, forDelete);
}

void handleDeleteObject(AsyncWebServerRequest* request, const Target& t) {
    if (const s3::HttpError r = checkWrite(t, true); r.status) {
        sendHttpError(request, r);
        return;
    }
    if (t.bucket->isMedia()) {
        if (s3::mediaKeyDeleteProtected(*t.bucket, t.key, t.keyLen)) {
            sendXmlError(request, 403, "AccessDenied", "system font cannot be deleted");
            return;
        }
        char key[MEDIA_KEY_MAX + 1];
        copyMediaKey(t, key);
        const media_err_t e = media_delete(t.bucket->mediaBucket(), key);
        if (const s3::HttpError r = s3::mediaError(e, s3::MediaOp::Delete); r.status) {
            sendHttpError(request, r);
            return;
        }
        if (e == MEDIA_OK) {  // not for a key that was not there
            ESP_LOGI(TAG, "DELETE %s", request->url().c_str());
            notifyMediaChanged(*t.bucket);
        }
        AsyncWebServerResponse* response = request->beginResponse(204);
        request->send(response);
        return;
    }
    if (s3::keyIsConfig(*t.bucket, t.key, t.keyLen)) {
        sendXmlError(request, 403, "AccessDenied", "config.json cannot be deleted");
        return;
    }
    char path[kMountMax + 1 + s3::kWriteKeyMax + 1];
    if (!buildPath(*t.bucket, path, sizeof path, t.key, t.keyLen)) {
        sendHttpError(request, s3::kNotMounted);
        return;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        if (errno == EBUSY) sendHttpError(request, s3::kFileInUse);
        else sendXmlError(request, 500, "InternalError", "delete failed");
        return;
    }
    AsyncWebServerResponse* response = request->beginResponse(204);
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
    if (st->written + len > st->limit) {
        st->fail(413, "EntityTooLarge", st->tooLargeMsg);
        return;
    }
    if (st->lengthKnown && st->written + len > st->expected) {
        st->fail(400, "InvalidRequest", "body longer than the declared length");
        return;
    }
    st->crc = esp_rom_crc32_le(st->crc, data, static_cast<uint32_t>(len));
    if (st->md5Live) mbedtls_md5_update(&st->md5, data, len);
    if (st->payload == fbauth::Payload::Sha256 || st->payload == fbauth::Payload::Streaming) st->sha.update(data, len);
    st->written += static_cast<uint32_t>(len);
    if (st->writer) {
        // Straight from the TCP buffer into flash. Not yet trusted: the
        // chunk signature / body hash are checked at chunk / body end, and
        // nothing becomes visible before media_put_commit.
        if (const media_err_t e = media_put_write(st->writer, data, static_cast<uint32_t>(len)); e != MEDIA_OK) {
            st->failMedia(e);
        }
        return;
    }
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

// First body byte (or end of an empty-body PUT): validate and open the temp
// file, or reserve the space in the media store.
PutState* putBegin(AsyncWebServerRequest* request) {
    PutState* st = allocState<PutState>(request);
    if (!st) return nullptr;
    request->onDisconnect([request]() { releaseState(request); });

    // Every write is signed. Verified before anything touches the
    // filesystem; a refused body is read and dropped.
    {
        fbauth::Result auth;
        if (!fbauth::verifyRequest(request, auth)) {
            if (isReplay(auth)) {
                st->fail(503, "SlowDown", auth.message);
            } else {
                st->fail(403, fbauth::statusCode(auth.status), auth.message);
            }
            st->authFailed = true;
            return st;
        }
        st->payload = auth.payload;
        if (auth.payload == fbauth::Payload::Sha256) {
            memcpy(st->wantSha, auth.payloadSha256, sizeof st->wantSha);
            st->sha.init();
        } else if (auth.payload == fbauth::Payload::Streaming) {
            st->sha.init();
            if (!st->chunks.begin(auth)) {
                st->fail(403, "AccessDenied", "cannot verify the chunk signatures");
                st->authFailed = true;
                return st;
            }
        }
    }

    const Target t = parseTarget(request->url());
    if (const s3::HttpError r = checkWrite(t); r.status) {
        st->fail(r);
        return st;
    }
    const bool media = t.bucket->isMedia();
    st->limit = s3::objectSizeLimit(*t.bucket);
    st->tooLargeMsg = s3::tooLargeMessage(*t.bucket);
    if (request->hasParam("uploads") || request->hasParam("uploadId") || request->hasHeader("x-amz-copy-source")) {
        st->fail(501, "NotImplemented", "multipart upload and copy are not supported");
        return st;
    }
    st->isConfig = s3::keyIsConfig(*t.bucket, t.key, t.keyLen);

    const char* encoding = headerValue(request, "Content-Encoding");
    const char* sha = headerValue(request, "x-amz-content-sha256");
    st->chunked = (encoding && strstr(encoding, "aws-chunked")) || (sha && strncmp(sha, "STREAMING-", 10) == 0);
    const bool chunkedPayload =
        st->payload == fbauth::Payload::Streaming || st->payload == fbauth::Payload::UnsignedStreaming;
    if (st->payload != fbauth::Payload::Unsigned && st->chunked != chunkedPayload) {
        // a body hash over the aws-chunked encoding, or chunk signatures on a plain body
        st->fail(400, "InvalidRequest", "x-amz-content-sha256 does not match the body encoding");
        return st;
    }
    const s3::PutLength length =
        s3::putLength(*t.bucket, st->chunked, headerValue(request, "x-amz-decoded-content-length"),
                      request->hasHeader("Content-Length"), request->contentLength());
    if (length.error.status) {
        st->fail(length.error);
        return st;
    }
    st->lengthKnown = length.known;
    st->expected = length.expected;
    if (const char* md5 = headerValue(request, "Content-MD5")) {
        if (s3::base64Decode(md5, strlen(md5), st->wantMd5, sizeof st->wantMd5) != 16) {
            st->fail(400, "InvalidDigest", "Content-MD5 is not a base64 MD5");
            return st;
        }
        st->haveMd5 = true;
    }
    if (st->haveMd5 || media) {  // media: the stored MD5 is the object's ETag
        mbedtls_md5_init(&st->md5);
        mbedtls_md5_starts(&st->md5);
        st->md5Live = true;
    }
    if (const char* crc = headerValue(request, "x-amz-checksum-crc32")) {
        if (!s3::parseCrc32B64(crc, strlen(crc), &st->wantCrc)) {
            st->fail(400, "InvalidRequest", "invalid x-amz-checksum-crc32");
            return st;
        }
        st->haveCrc = true;
    }
    if (media) {
        // Reserve exactly the decoded size before any body byte is written
        // (fails with NO_SPACE without writing anything). The store
        // serialises uploads itself (BUSY), so no s_putActive slot: it
        // guards LittleFS files only (s3_idle).
        char key[MEDIA_KEY_MAX + 1];
        copyMediaKey(t, key);
        const uint32_t mtime = s3::mediaMtime(fbauth::clockValid(), static_cast<int64_t>(time(nullptr)));
        media_writer_t* w = nullptr;
        const media_err_t e = media_put_begin(t.bucket->mediaBucket(), key, st->expected, mtime, &w);
        if (e != MEDIA_OK) {
            st->failMedia(e);
            return st;
        }
        st->writer = w;
        return st;
    }
    if (s_putActive) {
        st->fail(503, "SlowDown", "another upload is in progress, retry");
        return st;
    }
    const size_t finalLen = buildPath(*t.bucket, st->path, sizeof st->path, t.key, t.keyLen, s3::kTmpSuffix);
    if (!finalLen) {
        st->path[0] = '\0';
        st->fail(s3::kNotMounted);
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

void putFeed(PutState* st, const uint8_t* data, size_t len) {
    if (st->status || (st->fd < 0 && !st->writer)) return;
    if (!st->chunked) {
        putSink(st, data, len);
        return;
    }
    const auto onData = [st](const uint8_t* p, size_t n) { putSink(st, p, n); };
    if (st->payload == fbauth::Payload::Streaming) {
        // STREAMING-AWS4-HMAC-SHA256-PAYLOAD: each chunk carries a signature
        // chained from the request's; verified as soon as its data is in.
        const auto onChunkEnd = [st](const char* sig, size_t sigLen) {
            uint8_t digest[32];
            st->sha.finish(digest);
            st->sha.init();
            if (st->status) return true;  // already failed (e.g. too large): keep draining
            if (!st->chunks.verify(digest, fbauth::sv(sig, sigLen))) {
                st->fail(403, "SignatureDoesNotMatch", "aws-chunked chunk signature does not match");
                st->authFailed = true;
            }
            return true;
        };
        if (!st->dec.feed(data, len, onData, onChunkEnd)) st->fail(s3::kMalformedChunked);
        return;
    }
    if (!st->dec.feed(data, len, onData)) st->fail(s3::kMalformedChunked);
}

void putBody(AsyncWebServerRequest* request, const uint8_t* data, size_t len) {
    auto* st = static_cast<PutState*>(request->_tempObject);
    if (!st) {
        st = putBegin(request);
        if (!st) return;
    }
    if (st->kind != Kind::Put) return;
    putFeed(st, data, len);
    // A failed media upload gives its reservation back now, not only once
    // the rest of the body is drained.
    if (st->status) dropWriter(st);
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
        sendHttpError(request, s3::kOutOfMemory);
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
    if (!st->status && st->payload == fbauth::Payload::Sha256) {
        uint8_t digest[32];
        st->sha.finish(digest);
        if (!fbauth::digestEqual(digest, st->wantSha, 32)) {
            st->fail(400, "XAmzContentSHA256Mismatch", "x-amz-content-sha256 does not match the body");
        }
    }
    uint8_t md5[16] = {};
    if (!st->status && st->md5Live) {
        mbedtls_md5_finish(&st->md5, md5);
        if (st->haveMd5 && memcmp(md5, st->wantMd5, sizeof md5) != 0) {
            st->fail(400, "BadDigest", "Content-MD5 does not match the body");
        }
    }
    if (!st->status && st->chunked && st->dec.hasCrc32()) {
        uint32_t trailerCrc = 0;
        if (!s3::parseCrc32B64(st->dec.crc32B64(), strlen(st->dec.crc32B64()), &trailerCrc)) {
            st->fail(400, "InvalidRequest", "invalid x-amz-checksum-crc32 trailer");
        }
        st->haveCrc = true;  // compared below, like the header's
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

    const Target t = parseTarget(request->url());
    const bool media = t.type == Target::Object && t.bucket->isMedia();
    char finalPath[sizeof st->path];
    finalPath[0] = '\0';
    if (!st->status && media) {
        if (!st->writer) {
            st->fail(500, "InternalError", "upload not open");  // cannot happen: a failure sets status
        } else {
            // Atomic: until here the previous version (if any) stays live.
            // The commit consumes the writer, also when it fails.
            media_writer_t* w = st->writer;
            st->writer = nullptr;
            const media_err_t e = media_put_commit(w, md5);
            if (e != MEDIA_OK) {
                st->failMedia(e);
            } else {
                st->committed = true;
            }
        }
    } else if (!st->status) {
        memcpy(finalPath, st->path, st->finalLen);
        finalPath[st->finalLen] = '\0';
        if (rename(st->path, finalPath) != 0) {
            if (errno == EBUSY) st->fail(s3::kFileInUse);
            else st->fail(500, "InternalError", "rename failed");
        } else {
            st->committed = true;
        }
    }

    if (st->status) {
        char msg[112];
        const char* text = st->errMsg;
        if (st->noSpace) {
            media_usage_t u{};
            media_usage(&u);
            s3::formatNoSpaceMessage(st->expected, u.free, u.reclaimable, msg, sizeof msg);
            text = msg;
        } else if (!text) {
            snprintf(msg, sizeof msg, "invalid JSON at offset %lu", static_cast<unsigned long>(st->jsonOffset));
            text = msg;
        }
        ESP_LOGW(TAG, "PUT %s: %u %s %s", request->url().c_str(), st->status, st->errCode, text);
        const uint16_t status = st->status;
        const char* code = st->errCode;
        const bool authDenied = st->authFailed;
        releaseState(request);  // drop the temp file before answering
        sendXmlError(request, status, code, text, authDenied);
        return;
    }

    ESP_LOGI(TAG, "PUT %s: %lu bytes", request->url().c_str(), static_cast<unsigned long>(st->written));
    AsyncWebServerResponse* response = request->beginResponse(200);
    char etag[40];
    if (media) {
        s3::formatMd5ETag(md5, etag);
        notifyMediaChanged(*t.bucket);
    } else {
        struct stat fst{};
        stat(finalPath, &fst);
        s3::formatETag(st->written, static_cast<uint64_t>(fst.st_mtime), etag);
    }
    response->addHeader("ETag", etag);
    if (st->haveCrc) {
        char crc[9];
        s3::formatCrc32B64(st->crc, crc);
        response->addHeader("x-amz-checksum-crc32", crc);
    }
    if (st->isConfig) response->addHeader("x-freebutton-reboot-required", "true");
    releaseState(request);
    request->send(response);
}

// ---------------------------------------------------------------------------
// Handler
// ---------------------------------------------------------------------------

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
        // Not a CORS preflight (the CORS middleware answered those): an
        // OPTIONS from the same origin or without an Origin. No signature
        // is asked for a method the API does not have.
        if (method == HTTP_OPTIONS) {
            sendXmlError(request, 405, "MethodNotAllowed",
                         "The specified method is not allowed against this resource.");
            return;
        }
        // Signatures: reads of the public buckets and ListBuckets are
        // anonymous; everything else is signed (PutObject: see putBegin).
        const bool putObject = method == HTTP_PUT && t.type == Target::Object;
        const bool isRead = method == HTTP_GET || method == HTTP_HEAD;
        if (!putObject && !s3::anonymousAllowed(t.type == Target::Root ? nullptr : t.bucket, isRead)) {
            fbauth::Result auth;
            if (!verifyNoBody(request, auth)) {
                sendAuthError(request, auth);
                return;
            }
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
                    if (t.bucket->isMedia()) {
                        if (const media_err_t e = probeMediaBucket(*t.bucket); e != MEDIA_OK) {
                            sendHttpError(request, s3::mediaError(e, s3::MediaOp::Read));
                            return;
                        }
                    }
                    AsyncWebServerResponse* response = request->beginResponse(200);
                    response->addHeader("x-amz-bucket-region", "us-east-1");
                    addUsageHeaders(response, *t.bucket);
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
    return !s_putActive && stream::active() == 0;
}

void s3_register(AsyncWebServer& server) {
    server.addHandler(new S3Handler());
}
