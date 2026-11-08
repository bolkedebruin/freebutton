//
// Native unit tests for the SigV4 request authentication logic
// (src/core/AuthLogic.h): the AWS SigV4 test suite (header and presigned
// forms), the S3 SigV4 documentation examples (incl. aws-chunked), requests
// signed by botocore the way the IDE / S3 SDK will sign them, the error
// paths, and the credential format.
//
#include <unity.h>
#include <string>
#include <utility>
#include <vector>

#include "../../src/core/AuthLogic.h"
#include "sha256_portable.h"
#include "sigv4_vectors.h"
#include "boto_vectors.h"

using namespace fbauth;
using Sha = PortableSha256;

void setUp() {}
void tearDown() {}

// --- a request as ESPAsyncWebServer hands it over ------------------------------

// urlDecode() of ESPAsyncWebServer: %XX, and '+' becomes a space.
static std::string serverDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

struct MockReq {
    std::string m, p;
    std::vector<std::pair<std::string, std::string>> q, h;
    size_t len = 0;

    MockReq(const char* method, const char* target, const char* const* headers, const char* body = "") : m(method) {
        std::string t(target);
        const size_t qm = t.find('?');
        p = serverDecode(t.substr(0, qm));
        if (qm != std::string::npos) {
            const std::string qs = t.substr(qm + 1);
            size_t start = 0;
            while (start < qs.size()) {
                size_t end = qs.find('&', start);
                if (end == std::string::npos) end = qs.size();
                size_t eq = qs.find('=', start);
                if (eq == std::string::npos || eq > end) eq = end;
                std::string name = serverDecode(qs.substr(start, eq - start));
                std::string value = eq + 1 < end ? serverDecode(qs.substr(eq + 1, end - eq - 1)) : "";
                if (!name.empty()) q.emplace_back(name, value);
                start = end + 1;
            }
        }
        for (const char* const* it = headers; it && *it; it += 2) h.emplace_back(it[0], it[1]);
        len = strlen(body);
    }
    sv method() const { return m; }
    sv path() const { return p; }
    size_t queryCount() const { return q.size(); }
    sv queryName(size_t i) const { return q[i].first; }
    sv queryValue(size_t i) const { return q[i].second; }
    size_t headerCount() const { return h.size(); }
    sv headerName(size_t i) const { return h[i].first; }
    sv headerValue(size_t i) const { return h[i].second; }
    size_t contentLength() const { return len; }
};

struct StrSink {
    std::string s;
    void Put(char c) { s += c; }
};

static std::string hex(const uint8_t* d, size_t n) {
    std::string s(2 * n, '0');
    toHex(d, n, s.data());
    return s;
}

static std::string sha256Hex(sv s) {
    uint8_t d[32];
    sha256<Sha>(reinterpret_cast<const uint8_t*>(s.data()), s.size(), d);
    return hex(d, 32);
}

static Status verifyWith(const MockReq& req, sv akid, sv secret, sv service, int64_t now, Result& r,
                         sv presignedPayload = kUnsignedPayload) {
    VerifyConfig cfg;
    cfg.accessKeyId = akid;
    cfg.service = service;
    cfg.now = now;
    cfg.presignedPayload = presignedPayload;
    const std::string sec(secret);
    const std::string svc(service);
    return verify<Sha>(req, cfg,
                       [&](sv date8, uint8_t out[32]) {
                           return deriveSigningKey<Sha>(sec, date8, kRegion, svc, out);
                       },
                       r);
}

static std::string after(sv s, sv key, char stop) {
    const size_t at = s.find(key);
    if (at == sv::npos) return "";
    const size_t b = at + key.size();
    const size_t e = s.find(stop, b);
    return std::string(s.substr(b, e == sv::npos ? sv::npos : e - b));
}

static int64_t epochOf(const char* amzDate) {
    int64_t e = 0;
    TEST_ASSERT_TRUE(parseAmzDate(amzDate, e));
    return e;
}

// --- primitives ---------------------------------------------------------------

void test_sha256_and_hmac_known_answers() {
    TEST_ASSERT_EQUAL_STRING(kEmptySha256.data(), sha256Hex("").c_str());
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", sha256Hex("abc").c_str());
    TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                             sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
    // RFC 4231 test case 2
    uint8_t mac[32];
    hmac<Sha>(reinterpret_cast<const uint8_t*>("Jefe"), 4, "what do ya want for nothing?", mac);
    TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", hex(mac, 32).c_str());
}

void test_signing_key_aws_example() {
    // AWS docs "Deriving the signing key" example (service iam, 20120215).
    uint8_t key[32];
    TEST_ASSERT_TRUE(deriveSigningKey<Sha>("wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY", "20120215", "us-east-1", "iam", key));
    TEST_ASSERT_EQUAL_STRING("f4780e2d9f65fa895f9c67b32ce1baf0b0d8a43505a000a1a9e090d414db404d", hex(key, 32).c_str());
}

void test_amz_date_roundtrip() {
    TEST_ASSERT_EQUAL_INT64(1440938160, epochOf("20150830T123600Z"));
    TEST_ASSERT_EQUAL_INT64(1369353600, epochOf("20130524T000000Z"));
    char buf[17];
    formatAmzDate(1440938160, buf);
    TEST_ASSERT_EQUAL_STRING("20150830T123600Z", buf);
    formatAmzDate(951782400, buf);  // leap day 2000
    TEST_ASSERT_EQUAL_STRING("20000229T000000Z", buf);
    formatAmzDate(epochOf("20991231T235959Z"), buf);
    TEST_ASSERT_EQUAL_STRING("20991231T235959Z", buf);
    char http[30];
    formatHttpDate(epochOf("20261108T120000Z"), http);
    TEST_ASSERT_EQUAL_STRING("Sun, 08 Nov 2026 12:00:00 GMT", http);
    formatHttpDate(0, http);
    TEST_ASSERT_EQUAL_STRING("Thu, 01 Jan 1970 00:00:00 GMT", http);
    formatHttpDate(951782400, http);
    TEST_ASSERT_EQUAL_STRING("Tue, 29 Feb 2000 00:00:00 GMT", http);
    int64_t e;
    TEST_ASSERT_FALSE(parseAmzDate("20150830123600Z", e));
    TEST_ASSERT_FALSE(parseAmzDate("20151330T123600Z", e));
    TEST_ASSERT_FALSE(parseAmzDate("2015083OT123600Z", e));
    TEST_ASSERT_FALSE(parseAmzDate("20150830T123600", e));
}

void test_uri_encoding_and_query_order() {
    StrSink s;
    uriEncode(s, "/a b/c+d$e~f/\xE1\x88\xB4", true);
    TEST_ASSERT_EQUAL_STRING("/a%20b/c%2Bd%24e~f/%E1%88%B4", s.s.c_str());
    StrSink q;
    uriEncode(q, "scripts/x=y&z", false);
    TEST_ASSERT_EQUAL_STRING("scripts%2Fx%3Dy%26z", q.s.c_str());

    // Sorted by the ENCODED name: "%E1..." < "Param" < "Param-3" < "a"; ties by value.
    static const char* const host[] = {"Host", "h", nullptr};
    MockReq r("GET", "/?b=2&a=2&a=1&Param-3=x&Param=y&%E1%88%B4=z&a=1", host);
    StrSink out;
    writeCanonicalQuery(r, false, out);
    TEST_ASSERT_EQUAL_STRING("%E1%88%B4=z&Param=y&Param-3=x&a=1&a=1&a=2&b=2", out.s.c_str());
    TEST_ASSERT_TRUE(compareEncoded("a", "ab") < 0);
    TEST_ASSERT_TRUE(compareEncoded("~", "%") > 0);
    TEST_ASSERT_TRUE(compareEncoded(" ", "!") < 0);  // %20 < %21
}

void test_header_value_trim() {
    StrSink s;
    putTrimmedHeaderValue(s, "  a \t b\n   c  ");
    TEST_ASSERT_EQUAL_STRING("a b c", s.s.c_str());
}

// --- AWS SigV4 test suite -------------------------------------------------------

void test_suite_header_form() {
    int n = 0;
    for (const auto& v : kSuite) {
        MockReq req(v.method, v.headerTarget, v.headerHeaders, v.body);
        sv auth;
        TEST_ASSERT_TRUE_MESSAGE(findHeader(req, "authorization", auth), v.name);
        const std::string signedHeaders = after(auth, "SignedHeaders=", ',');
        sv content;
        const std::string payload = findHeader(req, "x-amz-content-sha256", content) ? std::string(content)
                                                                                     : std::string(kEmptySha256);
        StrSink creq;
        TEST_ASSERT_TRUE_MESSAGE(writeCanonicalRequest(req, signedHeaders, payload, false, creq), v.name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(v.headerCanonical, creq.s.c_str(), v.name);

        Result r;
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::Ok, (int)verifyWith(req, v.accessKeyId, v.secret, v.service, 0, r), v.name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(v.headerSignature, hex(r.signature, 32).c_str(), v.name);
        TEST_ASSERT_FALSE(r.presigned);
        // signed at the suite's timestamp, checked at that time: still valid
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::Ok,
                                      (int)verifyWith(req, v.accessKeyId, v.secret, v.service, epochOf("20150830T123600Z"), r),
                                      v.name);
        // wrong secret
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::SignatureDoesNotMatch,
                                      (int)verifyWith(req, v.accessKeyId, "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEZ", v.service, 0, r),
                                      v.name);
        ++n;
    }
    TEST_ASSERT_EQUAL_INT(32, n);
}

void test_suite_presigned_form() {
    int n = 0;
    for (const auto& v : kSuite) {
        // post-sts-header-after: the signer adds X-Amz-Security-Token to the
        // URL AFTER signing (a signer-side quirk); a verifier must include
        // every query parameter, so this one cannot verify by design.
        if (strcmp(v.name, "post-sts-header-after") == 0) continue;
        ++n;
        MockReq req(v.method, v.queryTarget, v.queryHeaders, v.body);
        sv sh;
        TEST_ASSERT_TRUE(findQuery(req, "X-Amz-SignedHeaders", sh));
        StrSink creq;
        TEST_ASSERT_TRUE_MESSAGE(writeCanonicalRequest(req, sh, v.queryPayload, true, creq), v.name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(v.queryCanonical, creq.s.c_str(), v.name);

        Result r;
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::Ok,
                                      (int)verifyWith(req, v.accessKeyId, v.secret, v.service, 0, r, v.queryPayload), v.name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(v.querySignature, hex(r.signature, 32).c_str(), v.name);
        TEST_ASSERT_TRUE(r.presigned);
        TEST_ASSERT_TRUE(r.payload == Payload::Unsigned);
        // The suite signs the body hash, the device always UNSIGNED-PAYLOAD.
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::SignatureDoesNotMatch,
                                      (int)verifyWith(req, v.accessKeyId, v.secret, v.service, 0, r), v.name);
    }
    TEST_ASSERT_EQUAL_INT(31, n);
}

// --- S3 SigV4 documentation examples -----------------------------------------------

static constexpr const char* kDocAkid = "AKIAIOSFODNN7EXAMPLE";
static constexpr const char* kDocSecret = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
static constexpr const char* kDocDate = "20130524T000000Z";

static std::string docAuth(const char* signedHeaders, const char* sig) {
    return std::string("AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request,SignedHeaders=") +
           signedHeaders + ",Signature=" + sig;
}

void test_s3_docs_header_examples() {
    struct Case {
        const char* method;
        const char* target;
        const char* signedHeaders;
        const char* sig;
        std::vector<std::pair<std::string, std::string>> headers;
        const char* body;
    };
    const std::string empty(kEmptySha256);
    const std::string welcome = sha256Hex("Welcome to Amazon S3.");
    const Case cases[] = {
        {"GET", "/test.txt", "host;range;x-amz-content-sha256;x-amz-date",
         "f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41",
         {{"Host", "examplebucket.s3.amazonaws.com"}, {"Range", "bytes=0-9"}, {"x-amz-content-sha256", empty}, {"x-amz-date", kDocDate}},
         ""},
        {"PUT", "/test%24file.text", "date;host;x-amz-content-sha256;x-amz-date;x-amz-storage-class",
         "98ad721746da40c64f1a55b78f14c238d841ea1380cd77a1b5971af0ece108bd",
         {{"Host", "examplebucket.s3.amazonaws.com"}, {"Date", "Fri, 24 May 2013 00:00:00 GMT"}, {"x-amz-date", kDocDate},
          {"x-amz-storage-class", "REDUCED_REDUNDANCY"}, {"x-amz-content-sha256", welcome}},
         "Welcome to Amazon S3."},
        {"GET", "/?lifecycle", "host;x-amz-content-sha256;x-amz-date",
         "fea454ca298b7da1c68078a5d1bdbfbbe0d65c699e0f91ac7a200a0136783543",
         {{"Host", "examplebucket.s3.amazonaws.com"}, {"x-amz-date", kDocDate}, {"x-amz-content-sha256", empty}},
         ""},
        {"GET", "/?max-keys=2&prefix=J", "host;x-amz-content-sha256;x-amz-date",
         "34b48302e7b5fa45bde8084f4b7868a86f0a534bc59db6670ed5711ef69dc6f7",
         {{"Host", "examplebucket.s3.amazonaws.com"}, {"x-amz-date", kDocDate}, {"x-amz-content-sha256", empty}},
         ""},
    };
    for (const auto& c : cases) {
        MockReq req(c.method, c.target, nullptr, c.body);
        req.h = c.headers;
        req.h.emplace_back("Authorization", docAuth(c.signedHeaders, c.sig));
        Result r;
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::Ok, (int)verifyWith(req, kDocAkid, kDocSecret, "s3", 0, r), c.target);
        TEST_ASSERT_TRUE(r.payload == Payload::Sha256);
        TEST_ASSERT_EQUAL_STRING(sha256Hex(c.body).c_str(), hex(r.payloadSha256, 32).c_str());
    }
}

void test_s3_docs_presigned_example() {
    static const char* const host[] = {"Host", "examplebucket.s3.amazonaws.com", nullptr};
    MockReq req("GET",
                "/test.txt?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIOSFODNN7EXAMPLE%2F20130524%2Fus-east-1%2Fs3%2Faws4_request"
                "&X-Amz-Date=20130524T000000Z&X-Amz-Expires=86400&X-Amz-SignedHeaders=host"
                "&X-Amz-Signature=aeeed9bbccd4d02ee5c0109b86d86835f995330da4c265957d157751f604d404",
                host);
    Result r;
    const int64_t t = epochOf(kDocDate);
    TEST_ASSERT_EQUAL_INT((int)Status::Ok, (int)verifyWith(req, kDocAkid, kDocSecret, "s3", 0, r));
    TEST_ASSERT_EQUAL_INT((int)Status::Ok, (int)verifyWith(req, kDocAkid, kDocSecret, "s3", t + 86400, r));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)verifyWith(req, kDocAkid, kDocSecret, "s3", t + 86401, r));
    TEST_ASSERT_EQUAL_INT((int)Status::Ok, (int)verifyWith(req, kDocAkid, kDocSecret, "s3", t - 900, r));
    TEST_ASSERT_EQUAL_INT((int)Status::RequestTimeTooSkewed, (int)verifyWith(req, kDocAkid, kDocSecret, "s3", t - 901, r));
}

void test_s3_docs_aws_chunked_example() {
    MockReq req("PUT", "/examplebucket/chunkObject.txt", nullptr, "");
    req.h = {{"Host", "s3.amazonaws.com"},
             {"x-amz-date", kDocDate},
             {"x-amz-storage-class", "REDUCED_REDUNDANCY"},
             {"Authorization",
              docAuth("content-encoding;content-length;host;x-amz-content-sha256;x-amz-date;x-amz-decoded-content-length;x-amz-storage-class",
                      "4f232c4386841ef735655705268965c44a0e4690baa4adea153f7db9fa80a0a9")},
             {"x-amz-content-sha256", "STREAMING-AWS4-HMAC-SHA256-PAYLOAD"},
             {"Content-Encoding", "aws-chunked"},
             {"x-amz-decoded-content-length", "66560"},
             {"Content-Length", "66824"}};
    req.len = 66824;
    Result r;
    TEST_ASSERT_EQUAL_INT((int)Status::Ok, (int)verifyWith(req, kDocAkid, kDocSecret, "s3", 0, r));
    TEST_ASSERT_TRUE(r.payload == Payload::Streaming);
    TEST_ASSERT_EQUAL_STRING(kDocDate, r.amzDate);

    uint8_t key[32];
    deriveSigningKey<Sha>(kDocSecret, "20130524", kRegion, kService, key);
    const std::string a64k(65536, 'a'), a1k(1024, 'a');
    const char* expected[] = {"ad80c730a21e5b8d04586a2213dd63b9a0e99e0e2307b0ade35a65485a288648",
                              "0055627c9e194cb4542bae2aa5492e3c1575bbb81b612b7d234b86a503ef5497",
                              "b6c6ea8a5354eaf15b3cb7646744f4275b71ea724fed81ceb9323e279d449df9"};
    const std::string* chunks[] = {&a64k, &a1k, nullptr};
    uint8_t prev[32];
    memcpy(prev, r.signature, 32);
    for (int i = 0; i < 3; ++i) {
        uint8_t h[32], sig[32];
        const std::string data = chunks[i] ? *chunks[i] : std::string();
        sha256<Sha>(reinterpret_cast<const uint8_t*>(data.data()), data.size(), h);
        chunkSignature<Sha>(key, r.amzDate, kRegion, kService, prev, h, sig);
        TEST_ASSERT_EQUAL_STRING(expected[i], hex(sig, 32).c_str());
        memcpy(prev, sig, 32);
    }
}

// --- requests signed by botocore (as the IDE / S3 SDK sign them) ---------------------

static constexpr const char* kBotoAkid = "FBABCDEFGHIJKLMNOP23";
static constexpr const char* kBotoSecret = "abcdefghijABCDEFGHIJ0123456789klmnopqrst";

void test_botocore_requests() {
    const int64_t t = epochOf("20261108T120000Z");
    for (const auto& v : kBoto) {
        MockReq req(v.method, v.target, v.headers, v.body);
        Result r;
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::Ok, (int)verifyWith(req, kBotoAkid, kBotoSecret, "s3", t, r), v.name);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::Ok, (int)verifyWith(req, kBotoAkid, kBotoSecret, "s3", 0, r), v.name);
        if (r.presigned) {
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::Ok, (int)verifyWith(req, kBotoAkid, kBotoSecret, "s3", t + 300, r), v.name);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::AccessDenied,
                                          (int)verifyWith(req, kBotoAkid, kBotoSecret, "s3", t + 301, r), v.name);
        } else {
            TEST_ASSERT_TRUE(r.payload == Payload::Sha256);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(sha256Hex(v.body).c_str(), hex(r.payloadSha256, 32).c_str(), v.name);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::Ok, (int)verifyWith(req, kBotoAkid, kBotoSecret, "s3", t + 900, r), v.name);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::Ok, (int)verifyWith(req, kBotoAkid, kBotoSecret, "s3", t - 900, r), v.name);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::RequestTimeTooSkewed,
                                          (int)verifyWith(req, kBotoAkid, kBotoSecret, "s3", t + 901, r), v.name);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::RequestTimeTooSkewed,
                                          (int)verifyWith(req, kBotoAkid, kBotoSecret, "s3", t - 901, r), v.name);
        }
        // Tampering with the path, a signed header or the method breaks it.
        MockReq other = req;
        other.p += "x";
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::SignatureDoesNotMatch,
                                      (int)verifyWith(other, kBotoAkid, kBotoSecret, "s3", 0, r), v.name);
        other = req;
        other.h[0].second = "evil.example";  // Host
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::SignatureDoesNotMatch,
                                      (int)verifyWith(other, kBotoAkid, kBotoSecret, "s3", 0, r), v.name);
        other = req;
        other.m = "DELETE";
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::SignatureDoesNotMatch,
                                      (int)verifyWith(other, kBotoAkid, kBotoSecret, "s3", 0, r), v.name);
        // Another device's credentials
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::AccessDenied,
                                      (int)verifyWith(req, "FBZZZZZZZZZZZZZZZZZZ", kBotoSecret, "s3", 0, r), v.name);
    }
}

void test_extra_query_param_breaks_presigned() {
    const auto& v = kBoto[4];  // ws-presign
    std::string target = std::string(v.target) + "&inject=1";
    MockReq req(v.method, target.c_str(), v.headers);
    Result r;
    TEST_ASSERT_EQUAL_INT((int)Status::SignatureDoesNotMatch, (int)verifyWith(req, kBotoAkid, kBotoSecret, "s3", 0, r));
}

// --- error paths ---------------------------------------------------------------------

static Status headerCase(const std::string& auth, const char* date = "20261108T120000Z", const char* content = nullptr,
                         size_t len = 0, int64_t now = 0) {
    MockReq req("GET", "/config", nullptr);
    req.h = {{"Host", "192.168.1.50"}, {"Authorization", auth}};
    if (date) req.h.emplace_back("x-amz-date", date);
    if (content) req.h.emplace_back("x-amz-content-sha256", content);
    req.len = len;
    Result r;
    return verifyWith(req, kBotoAkid, kBotoSecret, "s3", now, r);
}

static std::string auth(const char* cred, const char* signedHeaders = "host;x-amz-date",
                        const char* sig = "0000000000000000000000000000000000000000000000000000000000000000") {
    return std::string("AWS4-HMAC-SHA256 Credential=") + cred + ", SignedHeaders=" + signedHeaders + ", Signature=" + sig;
}

void test_error_paths() {
    static const char* const host[] = {"Host", "192.168.1.50", nullptr};
    MockReq none("GET", "/config", host);
    Result r;
    TEST_ASSERT_EQUAL_INT((int)Status::MissingAuthentication, (int)verifyWith(none, kBotoAkid, kBotoSecret, "s3", 0, r));
    TEST_ASSERT_EQUAL_STRING("MissingAuthentication", statusCode(r.status));

    const char* good = "FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4_request";
    TEST_ASSERT_EQUAL_INT((int)Status::SignatureDoesNotMatch, (int)headerCase(auth(good)));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth("FBZZZZZZZZZZZZZZZZZZ/20261108/us-east-1/s3/aws4_request")));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth("FBABCDEFGHIJKLMNOP23/20261108/eu-west-1/s3/aws4_request")));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth("FBABCDEFGHIJKLMNOP23/20261108/us-east-1/iam/aws4_request")));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth("FBABCDEFGHIJKLMNOP23/20261109/us-east-1/s3/aws4_request")));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth("FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4")));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth("FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3")));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth(good, "x-amz-date")));          // host unsigned
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth(good, "host;x-amz-date;range")));  // header absent
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth(good, "host;x-amz-date", "zz")));  // bad sig
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth(good), nullptr));                // no x-amz-date
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth(good), "2026-11-08T12:00Z"));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth(good), "20261108T120000Z", nullptr, 5));  // body, no hash
    // accepted as a payload mode (S3 PutObject only), so this one fails on the signature
    TEST_ASSERT_EQUAL_INT((int)Status::SignatureDoesNotMatch, (int)headerCase(auth(good), "20261108T120000Z", "STREAMING-UNSIGNED-PAYLOAD-TRAILER"));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase(auth(good), "20261108T120000Z", "STREAMING-AWS4-HMAC-SHA256-PAYLOAD-TRAILER"));
    TEST_ASSERT_EQUAL_INT((int)Status::SignatureDoesNotMatch, (int)headerCase(auth(good), "20261108T120000Z", "UNSIGNED-PAYLOAD"));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase("Basic dXNlcjpwYXNz"));
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)headerCase("AWS4-HMAC-SHA256 SignedHeaders=host, Signature=00"));

    // Both mechanisms at once
    MockReq both("GET", "/ws/repl?X-Amz-Algorithm=AWS4-HMAC-SHA256", nullptr);
    both.h = {{"Host", "h"}, {"Authorization", auth(good)}};
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)verifyWith(both, kBotoAkid, kBotoSecret, "s3", 0, r));

    // Presigned: expiry bounds and missing parameters
    const std::string base = "/ws/repl?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=FBABCDEFGHIJKLMNOP23%2F20261108%2Fus-east-1%2Fs3%2Faws4_request"
                             "&X-Amz-Date=20261108T120000Z&X-Amz-SignedHeaders=host&X-Amz-Signature=" + std::string(64, '0');
    for (const char* exp : {"&X-Amz-Expires=604801", "&X-Amz-Expires=0", "&X-Amz-Expires=-5", "&X-Amz-Expires=abc", ""}) {
        MockReq p("GET", (base + exp).c_str(), host);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)Status::AccessDenied, (int)verifyWith(p, kBotoAkid, kBotoSecret, "s3", 0, r), exp);
    }
    MockReq ok("GET", (base + "&X-Amz-Expires=604800").c_str(), host);
    TEST_ASSERT_EQUAL_INT((int)Status::SignatureDoesNotMatch, (int)verifyWith(ok, kBotoAkid, kBotoSecret, "s3", 0, r));
    MockReq alg("GET", "/ws/repl?X-Amz-Algorithm=AWS4-HMAC-SHA1&X-Amz-Expires=5", host);
    TEST_ASSERT_EQUAL_INT((int)Status::AccessDenied, (int)verifyWith(alg, kBotoAkid, kBotoSecret, "s3", 0, r));
}

// A raw '+' in the path is decoded to a space by the server: documented
// limitation, clients send %2B (and then it verifies).
void test_plus_in_path() {
    static const char* const host[] = {"Host", "h", nullptr};
    MockReq encoded("GET", "/fs/a%2Bb", host);
    MockReq raw("GET", "/fs/a+b", host);
    StrSink e, w;
    uriEncode(e, encoded.path(), true);
    uriEncode(w, raw.path(), true);
    TEST_ASSERT_EQUAL_STRING("/fs/a%2Bb", e.s.c_str());
    TEST_ASSERT_EQUAL_STRING("/fs/a%20b", w.s.c_str());
}

// --- credentials ---------------------------------------------------------------------

void test_credentials_format() {
    uint32_t state = 12345;
    auto rng = [&state]() {
        state = state * 1103515245u + 12345u;
        return static_cast<uint8_t>(state >> 16);
    };
    char akid[kAccessKeyIdLen + 1], secret[kSecretLen + 1];
    for (int round = 0; round < 50; ++round) {
        generateCredentials(rng, akid, secret);
        TEST_ASSERT_EQUAL_size_t(20, strlen(akid));
        TEST_ASSERT_EQUAL_size_t(40, strlen(secret));
        TEST_ASSERT_TRUE(isValidAccessKeyId(akid));
        TEST_ASSERT_TRUE(isValidSecret(secret));
    }
    // Bytes >= 248 are rejected for the secret (no modulo bias).
    int calls = 0;
    auto high = [&calls]() { return static_cast<uint8_t>((calls++ % 2) ? 61 : 255); };
    generateCredentials(high, akid, secret);
    TEST_ASSERT_EQUAL_STRING(std::string(40, '9').c_str(), secret);

    TEST_ASSERT_FALSE(isValidAccessKeyId("FBABCDEFGHIJKLMNOP2"));
    TEST_ASSERT_FALSE(isValidAccessKeyId("FBABCDEFGHIJKLMNOP21"));  // '1' not base32
    TEST_ASSERT_FALSE(isValidAccessKeyId("AKABCDEFGHIJKLMNOP23"));
    TEST_ASSERT_FALSE(isValidAccessKeyId("FBabcdefghijklmnop23"));
    TEST_ASSERT_FALSE(isValidSecret("abc"));
    TEST_ASSERT_FALSE(isValidSecret(std::string(39, 'a') + "/"));

    char qr[kQrPayloadLen + 1];
    TEST_ASSERT_TRUE(formatQrPayload(kBotoAkid, kBotoSecret, qr, sizeof qr));
    TEST_ASSERT_EQUAL_STRING("FBCRED:FBABCDEFGHIJKLMNOP23:abcdefghijABCDEFGHIJ0123456789klmnopqrst", qr);
    TEST_ASSERT_FALSE(formatQrPayload(kBotoAkid, kBotoSecret, qr, sizeof qr - 1));

    char url[kQrUrlMax + 1];
    TEST_ASSERT_TRUE(formatQrUrl("192.168.1.51", kBotoAkid, kBotoSecret, url, sizeof url));
    TEST_ASSERT_EQUAL_STRING(
        "http://192.168.1.51/#fbcred=FBABCDEFGHIJKLMNOP23:abcdefghijABCDEFGHIJ0123456789klmnopqrst", url);
    TEST_ASSERT_TRUE(formatQrUrl("255.255.255.255", kBotoAkid, kBotoSecret, url, sizeof url));
    TEST_ASSERT_TRUE(strlen(url) <= 106);  // QR version 6 at ECC M: 3 px modules in the 140 px code
    TEST_ASSERT_FALSE(formatQrUrl("", kBotoAkid, kBotoSecret, url, sizeof url));
    TEST_ASSERT_FALSE(formatQrUrl("evil.host/x?", kBotoAkid, kBotoSecret, url, sizeof url));
    TEST_ASSERT_FALSE(formatQrUrl(std::string(33, 'a'), kBotoAkid, kBotoSecret, url, sizeof url));
    TEST_ASSERT_FALSE(formatQrUrl("10.0.0.1", kBotoAkid, kBotoSecret, url, sizeof url - 1));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_and_hmac_known_answers);
    RUN_TEST(test_signing_key_aws_example);
    RUN_TEST(test_amz_date_roundtrip);
    RUN_TEST(test_uri_encoding_and_query_order);
    RUN_TEST(test_header_value_trim);
    RUN_TEST(test_suite_header_form);
    RUN_TEST(test_suite_presigned_form);
    RUN_TEST(test_s3_docs_header_examples);
    RUN_TEST(test_s3_docs_presigned_example);
    RUN_TEST(test_s3_docs_aws_chunked_example);
    RUN_TEST(test_botocore_requests);
    RUN_TEST(test_extra_query_param_breaks_presigned);
    RUN_TEST(test_error_paths);
    RUN_TEST(test_plus_in_path);
    RUN_TEST(test_credentials_format);
    return UNITY_END();
}
