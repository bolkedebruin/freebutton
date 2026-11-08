//
// Auth - AWS Signature Version 4 authentication for the device's HTTP API.
//
// No TLS (no resources for HTTPS), so requests are signed instead: the secret
// never travels, the signature gives authenticity and integrity of what is
// signed (method, path, query, signed headers, and the body when its SHA-256
// is signed), and x-amz-date limits replays to a +-15 minute window (or the
// presigned URL's expiry). There is NO confidentiality: bodies and responses
// (e.g. GET /config, which contains secrets) cross the network in clear text.
// Unsynced clock (year < 2025): the time window is not checked (replays are
// then possible), the signature still is.
//
// Credentials: access key id "FB" + 18 base32 chars, secret 40 alphanumerics,
// generated on first boot from the hardware RNG and kept in NVS namespace
// "fbauth" (keys "akid", "secret"). They never go over the network: they are
// shown on the device (see DeviceInfoManager.cpp) as text and as a QR code
// "FBCRED:<akid>:<secret>". All requests are signed for region "us-east-1",
// service "s3" (also the non-S3 endpoints), algorithm AWS4-HMAC-SHA256.
//
// Usage for a handler (S3Server integration):
//
//   fbauth::Result auth;
//   if (!fbauth::verifyRequest(request, auth)) {        // header or presigned
//       // S3: XML <Error><Code>fbauth::statusCode(auth.status)</Code>
//       //      <Message>auth.message</Message></Error>, 403, plus
//       //      fbauth::addDeniedHeaders(response). Non-S3: fbauth::sendDenied().
//   }
//   switch (auth.payload) {
//     case fbauth::Payload::Unsigned:  break;              // body not covered
//     case fbauth::Payload::Sha256:    hash the body with fbauth::Sha256 while
//                                      it streams; before committing compare
//                                      with auth.payloadSha256 (mismatch ->
//                                      XAmzContentSHA256Mismatch / 400 in S3,
//                                      or SignatureDoesNotMatch 403)
//     case fbauth::Payload::Streaming: aws-chunked: fbauth::ChunkVerifier,
//                                      begin(auth), then verify() each chunk
//                                      (incl. the final empty one)
//   }
//
// verifyRequest() needs only the request line and headers, so call it as
// early as possible (canHandle / the first body callback) and refuse to
// write anything before it succeeded; cache the verdict in the per-request
// struct rather than verifying twice (the clock moves on).
//
// Cost: ~1 KB stack (two mbedTLS contexts + key material), no heap, a few
// hardware SHA-256 blocks; the signing key (4 HMACs) is cached per date.
//

#ifndef FREEBUTTON_AUTH_H
#define FREEBUTTON_AUTH_H

#include "AuthLogic.h"

#include <mbedtls/sha256.h>

class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebServerResponse;

namespace fbauth {

// mbedTLS SHA-256 (hardware on the ESP32-S3) with the Sha interface of
// AuthLogic.h. Also the streaming body hasher for the S3 server:
//   fbauth::Sha256 sha; sha.init(); sha.update(data, len) ...;
//   uint8_t d[32]; sha.finish(d); fbauth::digestEqual(d, auth.payloadSha256, 32)
// ~120 B; keep one per streaming request in the per-request struct.
class Sha256 {
public:
    Sha256() { mbedtls_sha256_init(&ctx_); }
    ~Sha256() { mbedtls_sha256_free(&ctx_); }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    void init() { mbedtls_sha256_starts(&ctx_, 0); }
    void update(const uint8_t* data, size_t len) { mbedtls_sha256_update(&ctx_, data, len); }
    void finish(uint8_t out[32]) { mbedtls_sha256_finish(&ctx_, out); }

private:
    mbedtls_sha256_context ctx_;
};

// Loads the credentials from NVS or creates them. Call once WiFi is started
// (the RF subsystem feeds the hardware RNG; webserver_start() does this).
void init();

// Verifies the request's SigV4 signature (Authorization header form or the
// presigned query form). True when result.status == Status::Ok. The payload
// mode / claimed body hash / seed signature are in result.
bool verifyRequest(AsyncWebServerRequest* request, Result& result);

// 403 {"status":"error","code":"...","message":"..."} + addDeniedHeaders().
void sendDenied(AsyncWebServerRequest* request, const Result& result);

// "x-freebutton-auth: required", and when the clock is valid "Date" (RFC 1123)
// and "x-amz-date" with the device time, so a client can correct its clock
// offset after RequestTimeTooSkewed and retry.
void addDeniedHeaders(AsyncWebServerResponse* response);

// Device clock usable for the time window (time() >= 2025-01-01).
bool clockValid();

// aws-chunked (STREAMING-AWS4-HMAC-SHA256-PAYLOAD) chunk signatures. One per
// streaming upload (~100 B, no heap). begin() takes the verified request's
// seed signature, date and the signing key; verify() checks one chunk given
// the SHA-256 of its data and the chunk-signature=<hex> from its header, and
// chains the signature for the next chunk. The final chunk is empty
// (sha256 of "").
class ChunkVerifier {
public:
    bool begin(const Result& verified);
    bool verify(const uint8_t chunkSha256[32], sv chunkSignatureHex);

private:
    uint8_t key_[32] = {};
    uint8_t prev_[32] = {};
    char amzDate_[17] = {};
    bool ready_ = false;
};

// Endpoints of WebServer.cpp that need a signature (see Auth.cpp for the
// list). Installs a gate handler that takes over a protected request whose
// signature fails, at the end of the headers (before any body is processed),
// and answers 403; plus a CORS middleware for cross-origin (dev) clients.
// Call FIRST in webserver_start(), before any other handler is added.
void installGate(AsyncWebServer& server);

// For the on-device credentials screen only (never send these anywhere).
void copyCredentials(char akid[kAccessKeyIdLen + 1], char secret[kSecretLen + 1]);

// New credentials (persisted); the old ones stop working immediately.
bool rotateCredentials();

}  // namespace fbauth

#endif  // FREEBUTTON_AUTH_H
