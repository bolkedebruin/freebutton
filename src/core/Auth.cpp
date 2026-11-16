//
// Auth - SigV4 verification on the device: credentials in NVS, the signing
// key cache, the ESPAsyncWebServer request adapter, the gate in front of the
// protected endpoints and the 403 answer. The SigV4 rules themselves are in
// AuthLogic.h (pure, natively tested). See Auth.h for the contract.
//

#include "Auth.h"

#include <ESPAsyncWebServer.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <nvs.h>
#include <time.h>

namespace fbauth {
namespace {

const char* TAG = "auth";

constexpr const char* kNvsNamespace = "fbauth";
constexpr const char* kNvsAkid = "akid";
constexpr const char* kNvsSecret = "secret";
constexpr time_t kClockValidFrom = 1735689600;  // 2025-01-01T00:00:00Z

// Credentials and the signing-key cache. Read on the async_tcp task, replaced
// by rotateCredentials() on the LVGL task: every access holds s_mux (a few
// dozen bytes copied, never a hash under the spinlock).
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
char s_akid[kAccessKeyIdLen + 1] = {};
char s_secret[kSecretLen + 1] = {};
bool s_ready = false;
uint32_t s_generation = 0;

struct KeyCache {
    char date8[8];
    uint8_t key[32];
    uint32_t generation;
    bool valid;
};
KeyCache s_cache = {};

// Protected endpoints (WebServer.cpp). A path matches exactly or with a
// sub-path ("/config/x"), as server.on()'s matcher does; every method but
// OPTIONS (CORS preflight stays anonymous).
constexpr const char* kProtected[] = {
    "/api/reboot",          // POST
    "/upload",              // POST multipart: firmware / filesystem image
    "/ota_update",          // POST: apply the upload (reboot)
    "/updatefirmware",      // POST: remote OTA
    "/resetWifi",           // POST: forget WiFi + reboot
    "/api/bytecode/upload", // POST multipart
    "/config",              // GET: contains secrets (WiFi / MQTT passwords)
    "/ws/repl",             // GET: WebSocket upgrade (presigned URL)
    "/api/screenshot",      // POST: repaint + write the displays to LittleFS
    "/api/heap/mark",       // POST: restart the heapLow measurement of /api/device
    "/api/bench",           // POST: repaint the displays N times (render timing)
};

uint8_t randomByte() {
    uint8_t b;
    esp_fill_random(&b, 1);
    return b;
}

bool storeCredentials(const char* akid, const char* secret) {
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(h, kNvsAkid, akid);
    if (err == ESP_OK) err = nvs_set_str(h, kNvsSecret, secret);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

void installCredentials(const char* akid, const char* secret) {
    portENTER_CRITICAL(&s_mux);
    memcpy(s_akid, akid, sizeof s_akid);
    memcpy(s_secret, secret, sizeof s_secret);
    ++s_generation;
    s_cache.valid = false;
    s_ready = true;
    portEXIT_CRITICAL(&s_mux);
}

// The signing key for a date: from the cache, or derived (4 HMACs) and cached.
bool signingKey(sv date8, uint8_t out[32]) {
    if (date8.size() != 8) return false;
    char secret[kSecretLen + 1];
    uint32_t generation;
    portENTER_CRITICAL(&s_mux);
    if (s_cache.valid && s_cache.generation == s_generation && memcmp(s_cache.date8, date8.data(), 8) == 0) {
        memcpy(out, s_cache.key, 32);
        portEXIT_CRITICAL(&s_mux);
        return true;
    }
    const bool ready = s_ready;
    memcpy(secret, s_secret, sizeof secret);
    generation = s_generation;
    portEXIT_CRITICAL(&s_mux);
    if (!ready) return false;

    const bool ok = deriveSigningKey<Sha256>(sv(secret, kSecretLen), date8, kRegion, kService, out);
    memset(secret, 0, sizeof secret);
    if (!ok) return false;
    portENTER_CRITICAL(&s_mux);
    if (generation == s_generation) {
        memcpy(s_cache.date8, date8.data(), 8);
        memcpy(s_cache.key, out, 32);
        s_cache.generation = generation;
        s_cache.valid = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return true;
}

sv view(const String& s) { return sv(s.c_str(), s.length()); }

// AuthLogic's Req over ESPAsyncWebServer: decoded path, URL query parameters
// only (form fields of a urlencoded body are not part of the canonical query).
struct RequestView {
    AsyncWebServerRequest* r;

    sv method() const { return r->methodToString(); }
    sv path() const { return view(r->url()); }
    const AsyncWebParameter* query(size_t i) const {
        size_t k = 0;
        for (size_t j = 0; j < r->params(); ++j) {
            const AsyncWebParameter* p = r->getParam(j);
            if (!p || p->isPost() || p->isFile()) continue;
            if (k++ == i) return p;
        }
        return nullptr;
    }
    size_t queryCount() const {
        size_t k = 0;
        for (size_t j = 0; j < r->params(); ++j) {
            const AsyncWebParameter* p = r->getParam(j);
            if (p && !p->isPost() && !p->isFile()) ++k;
        }
        return k;
    }
    sv queryName(size_t i) const {
        const AsyncWebParameter* p = query(i);
        return p ? view(p->name()) : sv();
    }
    sv queryValue(size_t i) const {
        const AsyncWebParameter* p = query(i);
        return p ? view(p->value()) : sv();
    }
    size_t headerCount() const { return r->headers(); }
    sv headerName(size_t i) const {
        const AsyncWebHeader* h = r->getHeader(i);
        return h ? view(h->name()) : sv();
    }
    sv headerValue(size_t i) const {
        const AsyncWebHeader* h = r->getHeader(i);
        return h ? view(h->value()) : sv();
    }
    size_t contentLength() const { return r->contentLength(); }
};

// --- replay and last known time (async_tcp task only) ----------------------
constexpr const char* kReplUrl = "/ws/repl";
constexpr uint32_t kMaxReplExpires = 300;
constexpr const char* kNvsMark = "mark";
constexpr uint32_t kMarkStep = 600;  // seconds between writes of the mark: flash wear

ReplayCache s_replay;
uint32_t s_mark = 0;  // newest time of an accepted request that NVS knows of

void loadMark() {
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return;
    nvs_get_u32(h, kNvsMark, &s_mark);
    nvs_close(h);
}

// Only called with a valid device clock, so a client with a wrong clock
// cannot push the mark into the future.
void rememberTime(int64_t t) {
    if (t <= static_cast<int64_t>(s_mark) + kMarkStep || t > 0xFFFFFFFFLL) return;
    s_mark = static_cast<uint32_t>(t);
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u32(h, kNvsMark, s_mark) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

bool isProtected(const String& url) {
    for (const char* p : kProtected) {
        const size_t n = strlen(p);
        if (url.length() >= n && strncmp(url.c_str(), p, n) == 0 && (url.length() == n || url[n] == '/')) {
            return true;
        }
    }
    return false;
}

// Signature plus the payload rule of the non-S3 endpoints: none of them
// hashes its body, so the body must be signed as UNSIGNED-PAYLOAD (the
// multipart uploads; the server hands the handlers only the decoded file
// parts, never the raw body a hash would cover), or the request has no body
// and claims the empty-body hash.
bool checkProtected(AsyncWebServerRequest* request, Result& res) {
    if (!verifyRequest(request, res)) return false;
    // A console URL is made when it is used: one that stays valid for long
    // is only useful to someone else.
    if (res.presigned && res.expires > kMaxReplExpires && request->url() == kReplUrl) {
        res.status = Status::AccessDenied;
        res.message = "X-Amz-Expires of the console URL must be at most 300";
        return false;
    }
    if (res.payload == Payload::Unsigned) return true;
    if (res.payload == Payload::Sha256 && request->contentLength() == 0) {
        uint8_t empty[32];
        fromHex(kEmptySha256, empty, 32);
        if (digestEqual(empty, res.payloadSha256, 32)) return true;
        res.status = Status::SignatureDoesNotMatch;
        res.message = "x-amz-content-sha256 does not match the empty body";
        return false;
    }
    res.status = Status::AccessDenied;
    res.message = "this endpoint does not check body hashes: sign the body as UNSIGNED-PAYLOAD";
    return false;
}

// Takes a protected request whose signature fails, at the end of the headers,
// before any body byte reaches the real handler (so a refused upload never
// touches flash). Trivial handler: the body is drained, not parsed. Valid
// requests are not taken and reach their handler unchanged.
class AuthGate final : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        if (request->method() == HTTP_OPTIONS || !isProtected(request->url())) return false;
        Result res;
        if (checkProtected(request, res)) return false;
        ESP_LOGW(TAG, "%s %s refused: %s (%s)", request->methodToString(), request->url().c_str(),
                 statusCode(res.status), res.message);
        return true;
    }

    void handleRequest(AsyncWebServerRequest* request) override {
        Result res;
        if (checkProtected(request, res)) {  // only if the clock moved into the window meanwhile
            res.status = Status::AccessDenied;
            res.message = "authentication failed";
        }
        sendDenied(request, res);
    }
};

// Cross-origin (dev: IDE served by Vite on another origin) only: same-origin
// requests (Origin == "http://" + Host, or no Origin) are left untouched.
bool isCrossOrigin(AsyncWebServerRequest* request) {
    const AsyncWebHeader* o = request->getHeader("Origin");
    if (!o) return false;
    const String& origin = o->value();
    const String& host = request->host();
    return !(origin.startsWith("http://") && origin.length() == 7 + host.length() &&
             strcmp(origin.c_str() + 7, host.c_str()) == 0);
}

void corsMiddleware(AsyncWebServerRequest* request, ArMiddlewareNext next) {
    if (!isCrossOrigin(request)) {
        next();
        return;
    }
    if (request->method() == HTTP_OPTIONS) {
        AsyncWebServerResponse* response = request->beginResponse(204);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, HEAD, DELETE");
        const AsyncWebHeader* requested = request->getHeader("Access-Control-Request-Headers");
        response->addHeader("Access-Control-Allow-Headers",
                            requested && requested->value().length()
                                ? requested->value().c_str()
                                : "authorization, x-amz-date, x-amz-content-sha256, content-type");
        response->addHeader("Access-Control-Max-Age", "600");
        request->send(response);
        return;
    }
    next();
    if (AsyncWebServerResponse* response = request->getResponse()) {
        // Do not replace what a handler set itself.
        response->addHeader("Access-Control-Allow-Origin", "*", false);
        response->addHeader("Access-Control-Expose-Headers", kExposeHeaders, false);
    }
}

}  // namespace

// ---------------------------------------------------------------------------

bool clockValid() { return time(nullptr) >= kClockValidFrom; }

void init() {
    char akid[kAccessKeyIdLen + 1] = {};
    char secret[kSecretLen + 1] = {};
    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK) {
        size_t na = sizeof akid, ns = sizeof secret;
        loaded = nvs_get_str(h, kNvsAkid, akid, &na) == ESP_OK && nvs_get_str(h, kNvsSecret, secret, &ns) == ESP_OK &&
                 isValidAccessKeyId(akid) && isValidSecret(secret);
        nvs_close(h);
    }
    if (!loaded) {
        generateCredentials(randomByte, akid, secret);
        if (!storeCredentials(akid, secret)) {
            ESP_LOGE(TAG, "could not store the API credentials in NVS (they change on every boot)");
        }
        ESP_LOGW(TAG, "new API credentials generated (access key id %s); show them on the device", akid);
    } else {
        ESP_LOGI(TAG, "API access key id %s", akid);
    }
    installCredentials(akid, secret);
    memset(secret, 0, sizeof secret);
    loadMark();
}

bool rotateCredentials() {
    char akid[kAccessKeyIdLen + 1];
    char secret[kSecretLen + 1];
    generateCredentials(randomByte, akid, secret);
    const bool stored = storeCredentials(akid, secret);
    if (stored) {
        installCredentials(akid, secret);
        ESP_LOGW(TAG, "API credentials rotated (access key id %s)", akid);
    } else {
        ESP_LOGE(TAG, "rotation failed: NVS write error, old credentials kept");
    }
    memset(secret, 0, sizeof secret);
    return stored;
}

void copyCredentials(char akid[kAccessKeyIdLen + 1], char secret[kSecretLen + 1]) {
    portENTER_CRITICAL(&s_mux);
    memcpy(akid, s_akid, kAccessKeyIdLen + 1);
    memcpy(secret, s_secret, kSecretLen + 1);
    portEXIT_CRITICAL(&s_mux);
}

bool verifyRequest(AsyncWebServerRequest* request, Result& result) {
    char akid[kAccessKeyIdLen + 1];
    portENTER_CRITICAL(&s_mux);
    const bool ready = s_ready;
    memcpy(akid, s_akid, sizeof akid);
    portEXIT_CRITICAL(&s_mux);
    if (!ready) {
        result = Result{};
        result.status = Status::AccessDenied;
        result.message = "credentials not initialised";
        return false;
    }
    VerifyConfig cfg;
    cfg.accessKeyId = sv(akid, kAccessKeyIdLen);
    const time_t now = time(nullptr);
    cfg.now = now >= kClockValidFrom ? static_cast<int64_t>(now) : 0;
    const RequestView req{request};
    if (verify<Sha256>(req, cfg, signingKey, result) != Status::Ok) return false;

    // The device clock is not set (no time check was possible): at least
    // nothing older than the last time it knew is accepted.
    if (cfg.now == 0 && s_mark != 0 && result.signedAt <= static_cast<int64_t>(s_mark)) {
        result.status = Status::RequestTimeTooSkewed;
        result.message = "x-amz-date is older than the device's last known time";
        return false;
    }
    // Whatever changes something is accepted once (AuthLogic.h, Replay):
    // every method but GET and HEAD, and the console WebSocket's upgrade.
    const auto method = request->method();
    if ((method != HTTP_GET && method != HTTP_HEAD) || request->url() == kReplUrl) {
        if (s_replay.check(result.signature, result.signedAt) != ReplayCache::Verdict::Fresh) {
            result.status = Status::RequestReplayed;
            result.message = "this signed request was used before: sign it again";
            return false;
        }
    }
    if (cfg.now > 0) rememberTime(result.signedAt < cfg.now ? result.signedAt : cfg.now);
    return true;
}

void addDeniedHeaders(AsyncWebServerResponse* response) {
    response->addHeader("x-freebutton-auth", "required");
    const time_t now = time(nullptr);
    if (now < kClockValidFrom) return;
    char date[30];
    formatHttpDate(static_cast<int64_t>(now), date);
    response->addHeader("Date", date);
    char amz[17];
    formatAmzDate(static_cast<int64_t>(now), amz);
    response->addHeader("x-amz-date", amz);
}

void sendDenied(AsyncWebServerRequest* request, const Result& result) {
    const Status status = result.status == Status::Ok ? Status::AccessDenied : result.status;
    char body[224];
    snprintf(body, sizeof body, R"({"status":"error","code":"%s","message":"%s"})", statusCode(status),
             result.message ? result.message : "");
    AsyncWebServerResponse* response = request->beginResponse(403, "application/json", body);
    addDeniedHeaders(response);
    request->send(response);
}

bool ChunkVerifier::begin(const Result& verified) {
    ready_ = false;
    if (verified.status != Status::Ok || verified.payload != Payload::Streaming) return false;
    if (!signingKey(sv(verified.amzDate, 8), key_)) return false;
    memcpy(prev_, verified.signature, 32);
    memcpy(amzDate_, verified.amzDate, sizeof amzDate_);
    ready_ = true;
    return true;
}

bool ChunkVerifier::verify(const uint8_t chunkSha256[32], sv chunkSignatureHex) {
    uint8_t claimed[32];
    if (!ready_ || !fromHex(chunkSignatureHex, claimed, 32)) return false;
    uint8_t expected[32];
    chunkSignature<Sha256>(key_, sv(amzDate_, 16), kRegion, kService, prev_, chunkSha256, expected);
    if (!digestEqual(expected, claimed, 32)) {
        ready_ = false;  // a broken chain stays broken
        return false;
    }
    memcpy(prev_, expected, 32);
    return true;
}

void installGate(AsyncWebServer& server) {
    server.addMiddleware(corsMiddleware);
    server.addHandler(new AuthGate());
}

}  // namespace fbauth
