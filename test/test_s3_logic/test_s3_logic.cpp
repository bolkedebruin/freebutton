//
// Native unit tests for the pure S3 API logic (src/core/S3Logic.h) and the
// streaming JSON validator (src/json/JsonValidator.h).
//
#include <unity.h>
#include <algorithm>
#include <string>
#include <vector>

#include "../../src/core/S3Logic.h"
#include "../../src/json/JsonValidator.h"
#include "rapidjson/stream.h"

void setUp() {}
void tearDown() {}

struct StrSink {
    std::string s;
    void Put(char c) { s += c; }
};

static const s3::Bucket& bucket(const char* name) {
    const s3::Bucket* b = s3::findBucket(name, strlen(name));
    TEST_ASSERT_NOT_NULL(b);
    return *b;
}
static s3::KeyCheck wk(const char* k) { return s3::checkWriteKey(bucket("app"), k, strlen(k)); }
static bool readable(const char* b, const char* k) { return s3::keyReadable(bucket(b), k, strlen(k)); }
static const char* owner(const char* b, const char* k) {
    const s3::Bucket* o = s3::keyOwner(bucket(b), k, strlen(k));
    return o ? o->name : "";
}

// --- buckets ----------------------------------------------------------------

void test_bucket_table() {
    TEST_ASSERT_EQUAL(4, s3::kBucketCount);
    TEST_ASSERT_EQUAL_STRING("", bucket("app").dir);
    TEST_ASSERT_TRUE(bucket("app").writable);
    TEST_ASSERT_EQUAL_STRING("www", bucket("web").dir);
    TEST_ASSERT_EQUAL(3, bucket("web").dirLen);
    TEST_ASSERT_EQUAL_STRING("scripts/examples", bucket("examples").dir);
    TEST_ASSERT_EQUAL(16, bucket("examples").dirLen);
    TEST_ASSERT_EQUAL_STRING("", bucket("fonts").dir);  // media store, not LittleFS
    TEST_ASSERT_TRUE(bucket("fonts").isMedia());
    TEST_ASSERT_EQUAL(MEDIA_BUCKET_FONTS, bucket("fonts").mediaBucket());
    TEST_ASSERT_FALSE(bucket("app").isMedia());
    TEST_ASSERT_FALSE(bucket("web").isMedia());
    TEST_ASSERT_FALSE(bucket("examples").isMedia());
    TEST_ASSERT_FALSE(bucket("web").writable);
    TEST_ASSERT_FALSE(bucket("examples").writable);
    TEST_ASSERT_TRUE(bucket("fonts").writable);
    TEST_ASSERT_TRUE(bucket("fonts").sharesStaticPath);
    TEST_ASSERT_FALSE(bucket("app").sharesStaticPath);
    TEST_ASSERT_NULL(s3::findBucket("fs", 2));
    TEST_ASSERT_NULL(s3::findBucket("ap", 2));
    TEST_ASSERT_NULL(s3::findBucket("apps", 4));
    TEST_ASSERT_NULL(s3::findBucket("", 0));
}

void test_list_buckets_xml_matches_table() {
    const std::string xml = s3::kListBucketsXml;
    size_t pos = 0;
    for (const s3::Bucket& b : s3::kBuckets) {
        const std::string tag = std::string("<Bucket><Name>") + b.name + "</Name>";
        const size_t at = xml.find(tag, pos);
        TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, b.name);  // present, in table order
        pos = at + tag.size();
    }
    size_t count = 0;
    for (size_t at = xml.find("<Bucket>"); at != std::string::npos; at = xml.find("<Bucket>", at + 1)) ++count;
    TEST_ASSERT_EQUAL(s3::kBucketCount, count);
    TEST_ASSERT_TRUE(xml.find("<Name>fs</Name>") == std::string::npos);
}

void test_parse_url() {
    auto t = s3::parseUrl("/", 1);
    TEST_ASSERT_TRUE(t.type == s3::UrlTarget::Root);
    t = s3::parseUrl("/app", 4);
    TEST_ASSERT_TRUE(t.type == s3::UrlTarget::BucketRoot);
    TEST_ASSERT_EQUAL_STRING("app", t.bucket->name);
    t = s3::parseUrl("/examples/", 10);
    TEST_ASSERT_TRUE(t.type == s3::UrlTarget::BucketRoot);
    TEST_ASSERT_EQUAL_STRING("examples", t.bucket->name);
    t = s3::parseUrl("/app/scripts/main.js", 20);
    TEST_ASSERT_TRUE(t.type == s3::UrlTarget::Object);
    TEST_ASSERT_EQUAL_STRING("app", t.bucket->name);
    TEST_ASSERT_EQUAL(15, t.keyLen);
    TEST_ASSERT_EQUAL_MEMORY("scripts/main.js", t.key, 15);
    t = s3::parseUrl("/web/index.html.gz", 18);
    TEST_ASSERT_TRUE(t.type == s3::UrlTarget::Object);
    TEST_ASSERT_EQUAL_STRING("web", t.bucket->name);
    TEST_ASSERT_EQUAL_MEMORY("index.html.gz", t.key, 13);
    // "//x" after the bucket stays in the key (rejected by the key checks).
    t = s3::parseUrl("/app//x.js", 10);
    TEST_ASSERT_TRUE(t.type == s3::UrlTarget::Object);
    TEST_ASSERT_EQUAL(5, t.keyLen);
    TEST_ASSERT_TRUE(s3::parseUrl("/fs", 3).type == s3::UrlTarget::Unknown);
    TEST_ASSERT_TRUE(s3::parseUrl("/fs/config.json", 15).type == s3::UrlTarget::Unknown);
    TEST_ASSERT_TRUE(s3::parseUrl("/app.css", 8).type == s3::UrlTarget::Unknown);
    TEST_ASSERT_TRUE(s3::parseUrl("/apps/x", 7).type == s3::UrlTarget::Unknown);
    TEST_ASSERT_TRUE(s3::parseUrl("", 0).type == s3::UrlTarget::Unknown);
}

void test_key_owner() {
    // app: the other buckets' directories, file and directory keys.
    TEST_ASSERT_EQUAL_STRING("web", owner("app", "www/index.html.gz"));
    TEST_ASSERT_EQUAL_STRING("web", owner("app", "www/"));
    // fonts is in the media store: a LittleFS fonts/ directory is app's.
    TEST_ASSERT_EQUAL_STRING("", owner("app", "fonts/mnt_medium_14.bin"));
    TEST_ASSERT_EQUAL_STRING("", owner("app", "fonts/"));
    TEST_ASSERT_EQUAL_STRING("examples", owner("app", "scripts/examples/button.js"));
    TEST_ASSERT_EQUAL_STRING("examples", owner("app", "scripts/examples/"));
    TEST_ASSERT_EQUAL_STRING("", owner("app", "scripts/"));
    TEST_ASSERT_EQUAL_STRING("", owner("app", "scripts/main.js"));
    TEST_ASSERT_EQUAL_STRING("", owner("app", "scripts/examplesx/a.js"));
    TEST_ASSERT_EQUAL_STRING("", owner("app", "scripts/examples"));  // a file of that name
    TEST_ASSERT_EQUAL_STRING("", owner("app", "www"));
    TEST_ASSERT_EQUAL_STRING("", owner("app", "wwwx/a.js"));
    TEST_ASSERT_EQUAL_STRING("", owner("app", "fontsx/a.js"));
    TEST_ASSERT_EQUAL_STRING("", owner("app", "config.json"));
    // The other buckets own their whole tree.
    TEST_ASSERT_EQUAL_STRING("", owner("web", "fonts/dm-sans-var.woff2"));
    TEST_ASSERT_EQUAL_STRING("", owner("web", "www/x"));
    TEST_ASSERT_EQUAL_STRING("", owner("examples", "examples/x.js"));
    TEST_ASSERT_EQUAL_STRING("", owner("fonts", "fonts/x"));
}

void test_bucket_paths() {
    char out[64];
    // Empty mount point (as mounted by the firmware).
    TEST_ASSERT_EQUAL(1, s3::bucketBase(bucket("app"), "", 0, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("/", out);
    TEST_ASSERT_EQUAL(5, s3::bucketBase(bucket("web"), "", 0, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("/www/", out);
    TEST_ASSERT_EQUAL(18, s3::bucketBase(bucket("examples"), "", 0, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("/scripts/examples/", out);
    TEST_ASSERT_EQUAL(12, s3::objectPath(bucket("app"), "", 0, "config.json", 11, "", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("/config.json", out);
    TEST_ASSERT_EQUAL_STRING("/scripts/main.js.tmp",
                             (s3::objectPath(bucket("app"), "", 0, "scripts/main.js", 15, ".tmp", out, sizeof out), out));
    s3::objectPath(bucket("web"), "", 0, "index.html.gz", 13, "", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("/www/index.html.gz", out);
    s3::objectPath(bucket("examples"), "", 0, "button.js", 9, "", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("/scripts/examples/button.js", out);
    // A real mount point.
    s3::objectPath(bucket("web"), "/littlefs", 9, "a.gz", 4, "", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("/littlefs/www/a.gz", out);
    TEST_ASSERT_EQUAL(10, s3::bucketBase(bucket("app"), "/littlefs", 9, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("/littlefs/", out);
    // Too small: 0, nothing promised about out.
    TEST_ASSERT_EQUAL(0, s3::objectPath(bucket("app"), "", 0, "config.json", 11, "", out, 12));
    TEST_ASSERT_EQUAL(12, s3::objectPath(bucket("app"), "", 0, "config.json", 11, "", out, 13));
    TEST_ASSERT_EQUAL(0, s3::bucketBase(bucket("examples"), "", 0, out, 18));
}

void test_write_rules_per_bucket() {
    TEST_ASSERT_TRUE(s3::checkWriteKey(bucket("web"), "a.js", 4) == s3::KeyCheck::ReadOnly);
    TEST_ASSERT_TRUE(s3::checkWriteKey(bucket("examples"), "button.js", 9) == s3::KeyCheck::ReadOnly);
    TEST_ASSERT_TRUE(s3::checkWriteKey(bucket("fonts"), "x.txt", 5) == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(s3::checkWriteKey(bucket("fonts"), "x.ttf", 5) == s3::KeyCheck::Ok);
    // Read-only even for keys that would be invalid anyway.
    TEST_ASSERT_TRUE(s3::checkWriteKey(bucket("web"), "../x", 4) == s3::KeyCheck::ReadOnly);
    // config.json / the bytecode index are special in app only.
    TEST_ASSERT_TRUE(s3::keyIsConfig(bucket("app"), "config.json", 11));
    TEST_ASSERT_FALSE(s3::keyIsConfig(bucket("examples"), "config.json", 11));
    TEST_ASSERT_FALSE(s3::keyIsConfig(bucket("app"), "scripts/config.json", 19));
    TEST_ASSERT_FALSE(s3::keyIsConfig(bucket("fonts"), "config.json", 11));
}

// --- keys -------------------------------------------------------------------

void test_write_key_accepts_plain_paths() {
    TEST_ASSERT_TRUE(wk("config.json") == s3::KeyCheck::Ok);
    TEST_ASSERT_TRUE(wk("scripts/main.js") == s3::KeyCheck::Ok);
    TEST_ASSERT_TRUE(wk("notes/a-b_c.txt") == s3::KeyCheck::Ok);
    TEST_ASSERT_TRUE(wk("scripts/examplesx.js") == s3::KeyCheck::Ok);
}

void test_write_key_rejects_traversal_and_shape() {
    TEST_ASSERT_TRUE(wk("") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("/config.json") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("../config.json") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("scripts/../config.json") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("a..b.js") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("scripts//main.js") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("scripts/") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("scripts/main js.js") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("a\x01.js") == s3::KeyCheck::Invalid);
}

void test_write_key_extensions() {
    TEST_ASSERT_TRUE(wk("main.jsc") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("index.html") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("x.JSON") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("x.json.tmp") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(wk("json") == s3::KeyCheck::Invalid);
}

void test_write_key_other_buckets_and_protected() {
    TEST_ASSERT_TRUE(wk("www/index.js") == s3::KeyCheck::OtherBucket);
    TEST_ASSERT_TRUE(wk("fonts/x.json") == s3::KeyCheck::Ok);  // fonts is not a LittleFS directory
    TEST_ASSERT_TRUE(wk("scripts/examples/x.js") == s3::KeyCheck::OtherBucket);
    TEST_ASSERT_TRUE(wk("js_bytecode_meta.json") == s3::KeyCheck::Forbidden);
    TEST_ASSERT_TRUE(wk("wwwx/a.js") == s3::KeyCheck::Ok);
    TEST_ASSERT_TRUE(wk("scripts/js_bytecode_meta.json") == s3::KeyCheck::Ok);
}

void test_write_key_lengths() {
    // 63 chars total is the maximum.
    std::string k = "abcd/" + std::string(55, 'a') + ".js";  // 5 + 55 + 3 = 63
    TEST_ASSERT_EQUAL(63, k.size());
    TEST_ASSERT_TRUE(wk(k.c_str()) == s3::KeyCheck::Ok);
    std::string k64 = "abcd/" + std::string(56, 'a') + ".js";
    TEST_ASSERT_TRUE(wk(k64.c_str()) == s3::KeyCheck::Invalid);
    // A single component must leave room for ".tmp" under name_max 64.
    std::string single = std::string(58, 'a') + ".js";  // 61 + 4 = 65 > 64
    TEST_ASSERT_TRUE(wk(single.c_str()) == s3::KeyCheck::Invalid);
    std::string single_ok = std::string(57, 'a') + ".js";  // 60 + 4 = 64
    TEST_ASSERT_TRUE(wk(single_ok.c_str()) == s3::KeyCheck::Ok);
}

void test_read_key() {
    TEST_ASSERT_TRUE(readable("web", "assets/index-abc.js.gz"));
    TEST_ASSERT_TRUE(readable("app", "config.json"));
    TEST_ASSERT_TRUE(readable("app", "js_bytecode_meta.json"));
    TEST_ASSERT_TRUE(readable("examples", "button.js"));
    TEST_ASSERT_FALSE(readable("web", "../config.json"));
    TEST_ASSERT_FALSE(readable("app", "www/../config.json"));
    TEST_ASSERT_FALSE(readable("app", ""));
    TEST_ASSERT_FALSE(readable("app", "a//b"));
    // Reachable through exactly one bucket.
    TEST_ASSERT_FALSE(readable("app", "www/index.html.gz"));
    TEST_ASSERT_TRUE(readable("app", "fonts/mnt_medium_14.bin"));  // fonts is in the media store
    TEST_ASSERT_FALSE(readable("app", "scripts/examples/button.js"));
    std::string longk(97, 'a');
    TEST_ASSERT_FALSE(readable("web", longk.c_str()));
}

void test_content_types() {
    TEST_ASSERT_EQUAL_STRING("application/json", s3::contentTypeFor("config.json", 11));
    TEST_ASSERT_EQUAL_STRING("application/javascript", s3::contentTypeFor("a/main.js", 9));
    TEST_ASSERT_EQUAL_STRING("text/plain", s3::contentTypeFor("x.txt", 5));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", s3::contentTypeFor("fonts/a.bin", 11));
    TEST_ASSERT_EQUAL_STRING("font/ttf", s3::contentTypeFor("medium.ttf", 10));
    TEST_ASSERT_EQUAL_STRING("font/otf", s3::contentTypeFor("medium.otf", 10));
}

// --- escaping / encoding -------------------------------------------------------

void test_xml_escape() {
    StrSink s;
    s3::putXml(s, "a&b<c>\"d'e");
    TEST_ASSERT_EQUAL_STRING("a&amp;b&lt;c&gt;&quot;d&apos;e", s.s.c_str());
}

void test_url_encode() {
    StrSink s;
    const char* k = "dir/a b+c%.js";
    s3::putUrlEncoded(s, k, strlen(k));
    TEST_ASSERT_EQUAL_STRING("dir/a%20b%2Bc%25.js", s.s.c_str());
}

void test_hex_token_roundtrip() {
    StrSink s;
    s3::putHex(s, "www/a.js", 8);
    char out[32];
    TEST_ASSERT_EQUAL(8, s3::hexDecode(s.s.data(), s.s.size(), out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("www/a.js", out);
    TEST_ASSERT_EQUAL(-1, s3::hexDecode("abc", 3, out, sizeof out));
    TEST_ASSERT_EQUAL(-1, s3::hexDecode("zz", 2, out, sizeof out));
    TEST_ASSERT_EQUAL(-1, s3::hexDecode("00", 2, out, sizeof out));
    TEST_ASSERT_EQUAL(-1, s3::hexDecode("6161", 4, out, 2));
}

void test_put_uint() {
    StrSink s;
    s3::putUint(s, 0);
    s.Put(',');
    s3::putUint(s, 18446744073709551615ull);
    TEST_ASSERT_EQUAL_STRING("0,18446744073709551615", s.s.c_str());
}

void test_base64() {
    uint8_t out[16];
    TEST_ASSERT_EQUAL(3, s3::base64Decode("TWFu", 4, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY("Man", out, 3);
    TEST_ASSERT_EQUAL(1, s3::base64Decode("TQ==", 4, out, sizeof out));
    TEST_ASSERT_EQUAL('M', out[0]);
    TEST_ASSERT_EQUAL(2, s3::base64Decode("TWE=", 4, out, sizeof out));
    TEST_ASSERT_EQUAL(-1, s3::base64Decode("TWE", 3, out, sizeof out));
    TEST_ASSERT_EQUAL(-1, s3::base64Decode("T!E=", 4, out, sizeof out));
    // MD5("") = d41d8cd98f00b204e9800998ecf8427e
    TEST_ASSERT_EQUAL(16, s3::base64Decode("1B2M2Y8AsgTpgAmY7PhCfg==", 24, out, sizeof out));
    TEST_ASSERT_EQUAL_HEX8(0xd4, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7e, out[15]);
    TEST_ASSERT_EQUAL(-1, s3::base64Decode("1B2M2Y8AsgTpgAmY7PhCfg==", 24, out, 8));
}

void test_crc32_b64() {
    // CRC32("hello world") = 0x0d4a1185 -> base64 of 0d 4a 11 85 = "DUoRhQ=="
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(s3::parseCrc32B64("DUoRhQ==", 8, &crc));
    TEST_ASSERT_EQUAL_HEX32(0x0d4a1185, crc);
    char out[9];
    s3::formatCrc32B64(0x0d4a1185, out);
    TEST_ASSERT_EQUAL_STRING("DUoRhQ==", out);
    s3::formatCrc32B64(0xffffffff, out);
    TEST_ASSERT_TRUE(s3::parseCrc32B64(out, 8, &crc));
    TEST_ASSERT_EQUAL_HEX32(0xffffffff, crc);
    TEST_ASSERT_FALSE(s3::parseCrc32B64("1B2M2Y8AsgTpgAmY7PhCfg==", 24, &crc));
}

// --- dates / etag ---------------------------------------------------------------

void test_dates() {
    char iso[25];
    s3::formatIso8601(0, iso);
    TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00.000Z", iso);
    s3::formatIso8601(1794141296, iso);  // 2026-11-08T12:34:56Z
    TEST_ASSERT_EQUAL_STRING("2026-11-08T12:34:56.000Z", iso);
    s3::formatIso8601(951782400, iso);  // leap day 2000-02-29
    TEST_ASSERT_EQUAL_STRING("2000-02-29T00:00:00.000Z", iso);
    s3::formatIso8601(4107542400ll, iso);  // 2100-03-01 (2100 is not a leap year)
    TEST_ASSERT_EQUAL_STRING("2100-03-01T00:00:00.000Z", iso);
    s3::formatIso8601(-5, iso);
    TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00.000Z", iso);
    char http[30];
    s3::formatHttpDate(784111777, http);  // RFC 7231 example
    TEST_ASSERT_EQUAL_STRING("Sun, 06 Nov 1994 08:49:37 GMT", http);
    s3::formatHttpDate(0, http);
    TEST_ASSERT_EQUAL_STRING("Thu, 01 Jan 1970 00:00:00 GMT", http);
}

void test_etag() {
    char e[40];
    s3::formatETag(10082, 0x6560a0b1, e);
    TEST_ASSERT_EQUAL_STRING("\"2762-6560a0b1\"", e);
    s3::formatETag(0, 0, e);
    TEST_ASSERT_EQUAL_STRING("\"0-0\"", e);
}

// --- roll-up ----------------------------------------------------------------------

void test_rollup() {
    bool isPrefix;
    TEST_ASSERT_EQUAL(4, s3::rollup("www/a.js", 8, 0, true, &isPrefix));
    TEST_ASSERT_TRUE(isPrefix);
    TEST_ASSERT_EQUAL(8, s3::rollup("www/a.js", 8, 4, true, &isPrefix));
    TEST_ASSERT_FALSE(isPrefix);
    TEST_ASSERT_EQUAL(8, s3::rollup("www/a.js", 8, 0, false, &isPrefix));
    TEST_ASSERT_FALSE(isPrefix);
    // prefix ending mid-segment
    TEST_ASSERT_EQUAL(11, s3::rollup("www/assets/x.js", 15, 5, true, &isPrefix));
    TEST_ASSERT_TRUE(isPrefix);
}

void test_dir_action() {
    size_t rolled = 0;
    TEST_ASSERT_TRUE(s3::dirAction("www/", 4, "", 0, true, &rolled) == s3::DirAction::Rollup);
    TEST_ASSERT_EQUAL(4, rolled);
    TEST_ASSERT_TRUE(s3::dirAction("www/", 4, "", 0, false, &rolled) == s3::DirAction::Descend);
    TEST_ASSERT_TRUE(s3::dirAction("www/", 4, "www/", 4, true, &rolled) == s3::DirAction::Descend);
    TEST_ASSERT_TRUE(s3::dirAction("www/assets/", 11, "www/", 4, true, &rolled) == s3::DirAction::Rollup);
    TEST_ASSERT_EQUAL(11, rolled);
    TEST_ASSERT_TRUE(s3::dirAction("www/", 4, "www/assets/", 11, true, &rolled) == s3::DirAction::Descend);
    TEST_ASSERT_TRUE(s3::dirAction("fonts/", 6, "www/", 4, true, &rolled) == s3::DirAction::Skip);
    TEST_ASSERT_TRUE(s3::dirAction("ww/", 3, "www", 3, true, &rolled) == s3::DirAction::Skip);
    TEST_ASSERT_TRUE(s3::dirAction("www/", 4, "ww", 2, true, &rolled) == s3::DirAction::Rollup);
    TEST_ASSERT_EQUAL(4, rolled);
}

void test_subtree_pruning() {
    // Everything under "a/" is <= "b" -> nothing new.
    TEST_ASSERT_TRUE(s3::subtreeNotAbove("a/", 2, "b"));
    // Marker inside the subtree -> must walk.
    TEST_ASSERT_FALSE(s3::subtreeNotAbove("a/", 2, "a/x"));
    // Marker is a prefix of the dir -> values are longer, so > marker.
    TEST_ASSERT_FALSE(s3::subtreeNotAbove("a/", 2, "a"));
    TEST_ASSERT_FALSE(s3::subtreeNotAbove("c/", 2, "b"));
    TEST_ASSERT_FALSE(s3::subtreeNotAbove("c/", 2, ""));

    TEST_ASSERT_TRUE(s3::subtreeAbove("c/", 2, "b.json"));
    TEST_ASSERT_FALSE(s3::subtreeAbove("a/", 2, "b.json"));
    TEST_ASSERT_FALSE(s3::subtreeAbove("a/", 2, "a/x"));
    TEST_ASSERT_TRUE(s3::subtreeAbove("a/", 2, "a"));
    TEST_ASSERT_FALSE(s3::subtreeAbove("a/", 2, nullptr));
}

// --- aws-chunked ---------------------------------------------------------------

struct DecodeResult {
    std::string data;
    bool ok;
    bool done;
    bool hasCrc;
    std::string crc;
};

static DecodeResult decodeSplit(const std::string& body, size_t split) {
    s3::AwsChunkedDecoder d;
    DecodeResult r{};
    auto sink = [&](const uint8_t* p, size_t n) { r.data.append(reinterpret_cast<const char*>(p), n); };
    const auto* p = reinterpret_cast<const uint8_t*>(body.data());
    r.ok = d.feed(p, split, sink) && d.feed(p + split, body.size() - split, sink);
    r.done = d.done();
    r.hasCrc = d.hasCrc32();
    r.crc = d.hasCrc32() ? d.crc32B64() : "";
    return r;
}

static void checkAllSplits(const std::string& body, const std::string& expect, bool expectCrc,
                           const char* crc) {
    for (size_t split = 0; split <= body.size(); ++split) {
        const DecodeResult r = decodeSplit(body, split);
        const std::string msg = "split at " + std::to_string(split);
        TEST_ASSERT_TRUE_MESSAGE(r.ok, msg.c_str());
        TEST_ASSERT_TRUE_MESSAGE(r.done, msg.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expect.c_str(), r.data.c_str(), msg.c_str());
        TEST_ASSERT_EQUAL_MESSAGE(expectCrc, r.hasCrc, msg.c_str());
        if (expectCrc) TEST_ASSERT_EQUAL_STRING_MESSAGE(crc, r.crc.c_str(), msg.c_str());
    }
    // Byte at a time.
    s3::AwsChunkedDecoder d;
    std::string out;
    for (char c : body) {
        const auto b = static_cast<uint8_t>(c);
        TEST_ASSERT_TRUE(d.feed(&b, 1, [&](const uint8_t* p, size_t n) {
            out.append(reinterpret_cast<const char*>(p), n);
        }));
    }
    TEST_ASSERT_TRUE(d.done());
    TEST_ASSERT_EQUAL_STRING(expect.c_str(), out.c_str());
}

void test_chunked_unsigned_trailer() {
    const std::string body =
        "b\r\nhello world\r\n"
        "0\r\n"
        "x-amz-checksum-crc32:DUoRhQ==\r\n"
        "\r\n";
    checkAllSplits(body, "hello world", true, "DUoRhQ==");
}

void test_chunked_signed() {
    const std::string sig = ";chunk-signature=ad80c730a21e5b8d04586a2213dd63b9a0e99e0e2307b0ade35a65485a288648";
    const std::string body =
        "5" + sig + "\r\nhello\r\n" +
        "6" + sig + "\r\n world\r\n" +
        "0" + sig + "\r\n\r\n";
    checkAllSplits(body, "hello world", false, "");
}

void test_chunked_signed_trailer_and_case() {
    const std::string body =
        "4\r\nabcd\r\n"
        "0\r\n"
        "X-Amz-Checksum-Crc32 : ignored-because-name-has-space\r\n"
        "x-amz-checksum-crc32c:AAAAAA==\r\n"
        "X-AMZ-CHECKSUM-CRC32: 7ZTn1A==\r\n"
        "x-amz-trailer-signature:abcdef\r\n"
        "\r\n";
    checkAllSplits(body, "abcd", true, "7ZTn1A==");
}

void test_chunked_empty_body() {
    checkAllSplits("0\r\n\r\n", "", false, "");
}

void test_chunked_errors() {
    auto bad = [](const std::string& body) {
        s3::AwsChunkedDecoder d;
        const bool ok = d.feed(reinterpret_cast<const uint8_t*>(body.data()), body.size(),
                               [](const uint8_t*, size_t) {});
        return !ok || !d.done();
    };
    TEST_ASSERT_TRUE(bad("zz\r\n"));
    TEST_ASSERT_TRUE(bad("\r\n"));
    TEST_ASSERT_TRUE(bad("3\nabc\r\n0\r\n\r\n"));
    TEST_ASSERT_TRUE(bad("3\r\nabcX\r\n0\r\n\r\n"));   // missing CRLF after data
    TEST_ASSERT_TRUE(bad("3\r\nabc\r\n"));             // truncated (no final chunk)
    TEST_ASSERT_TRUE(bad("fffffffff\r\n"));            // size overflow
    TEST_ASSERT_FALSE(bad("3\r\nabc\r\n0\r\n\r\n"));
}

// --- JSON validator -----------------------------------------------------------

static bool valid(const char* s, size_t* off) {
    rapidjson::StringStream ss(s);
    return json::validateJson(ss, off);
}

void test_json_validator() {
    size_t off = 0;
    TEST_ASSERT_TRUE(valid(R"({"a":[1,2,{"b":"c"}],"d":null})", &off));
    TEST_ASSERT_TRUE(valid("  {}\n", &off));
    TEST_ASSERT_FALSE(valid(R"({"a":1,})", &off));
    TEST_ASSERT_EQUAL(7, off);
    TEST_ASSERT_FALSE(valid(R"({"a":1} x)", &off));
    TEST_ASSERT_FALSE(valid("", &off));
    TEST_ASSERT_FALSE(valid(R"({"a":"unterminated)", &off));
    TEST_ASSERT_TRUE(valid(R"({"esc":"a\"b\\c\u00e9","n":-1.5e3,"t":true})", &off));
    TEST_ASSERT_TRUE(valid("[0,-0,1e300,0.5E-3]", &off));
    TEST_ASSERT_TRUE(valid("[123456789012345678901234567890]", &off));
    // RapidJSON (also the boot-time config parser) rejects exponents past double range.
    TEST_ASSERT_FALSE(valid("[1e400]", &off));
    TEST_ASSERT_FALSE(valid("[01]", &off));
    TEST_ASSERT_FALSE(valid("[1.]", &off));
    TEST_ASSERT_FALSE(valid("[-]", &off));
    TEST_ASSERT_FALSE(valid(R"(["bad \x escape"])", &off));
}

void test_json_validator_bounds() {
    size_t off = 0;
    std::string deep(64, '[');
    deep += std::string(64, ']');
    TEST_ASSERT_TRUE(valid(deep.c_str(), &off));
    std::string tooDeep(65, '[');
    tooDeep += std::string(65, ']');
    TEST_ASSERT_FALSE(valid(tooDeep.c_str(), &off));
    // A 60 KB string is fine (decoded in place, not onto the reader stack).
    std::string big = "{\"s\":\"" + std::string(60000, 'x') + "\"}";
    TEST_ASSERT_TRUE(valid(big.c_str(), &off));
}

// Signed aws-chunked (STREAMING-AWS4-HMAC-SHA256-PAYLOAD): every chunk's
// signature is handed over once its data is complete, the final empty chunk
// included; data still streams zero-copy.
static void test_chunked_signatures() {
    const std::string sigA(64, 'a'), sigB(64, 'b'), sigZ(64, 'f');
    const std::string body = "5;chunk-signature=" + sigA + "\r\nhello\r\n" +
                             "3;chunk-signature=" + sigB + "\r\nabc\r\n" +
                             "0;chunk-signature=" + sigZ + "\r\n\r\n";
    for (size_t split = 1; split <= body.size(); split += 7) {  // arbitrary TCP splits
        s3::AwsChunkedDecoder d;
        std::string data, events;
        auto onData = [&](const uint8_t* p, size_t n) { data.append(reinterpret_cast<const char*>(p), n); };
        auto onEnd = [&](const char* sig, size_t n) {
            events += std::to_string(data.size()) + ":" + std::string(sig, n).substr(0, 1) + std::to_string(n) + " ";
            return true;
        };
        const uint8_t* b = reinterpret_cast<const uint8_t*>(body.data());
        for (size_t off = 0; off < body.size(); off += split) {
            TEST_ASSERT_TRUE(d.feed(b + off, std::min(split, body.size() - off), onData, onEnd));
        }
        TEST_ASSERT_TRUE(d.done());
        TEST_ASSERT_EQUAL_STRING("helloabc", data.c_str());
        TEST_ASSERT_EQUAL_STRING("5:a64 8:b64 8:f64 ", events.c_str());
    }
    {   // a refusal stops the decoder
        s3::AwsChunkedDecoder d;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(body.data());
        TEST_ASSERT_FALSE(d.feed(b, body.size(), [](const uint8_t*, size_t) {},
                                 [](const char*, size_t) { return false; }));
        TEST_ASSERT_TRUE(d.failed());
    }
    {   // no / other / oversized extension: empty signature
        const std::string other = "2;foo=bar\r\nhi\r\n2\r\nho\r\n2;chunk-signature=" + std::string(65, 'c') +
                                  "\r\nhu\r\n0\r\n\r\n";
        s3::AwsChunkedDecoder d;
        std::string lens;
        TEST_ASSERT_TRUE(d.feed(reinterpret_cast<const uint8_t*>(other.data()), other.size(),
                                [](const uint8_t*, size_t) {},
                                [&](const char*, size_t n) { lens += std::to_string(n) + ","; return true; }));
        TEST_ASSERT_TRUE(d.done());
        TEST_ASSERT_EQUAL_STRING("0,0,0,0,", lens.c_str());
    }
}

// --- media store bucket (fonts) --------------------------------------------

static s3::KeyCheck fk(const char* k) { return s3::checkWriteKey(bucket("fonts"), k, strlen(k)); }

void test_media_write_keys() {
    TEST_ASSERT_TRUE(fk("medium.ttf") == s3::KeyCheck::Ok);
    TEST_ASSERT_TRUE(fk("semibold.ttf") == s3::KeyCheck::Ok);
    TEST_ASSERT_TRUE(fk("Icons_v2-1.otf") == s3::KeyCheck::Ok);
    TEST_ASSERT_TRUE(fk("a.ttf") == s3::KeyCheck::Ok);
    // Flat: no '/'.
    TEST_ASSERT_TRUE(fk("sub/medium.ttf") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("/medium.ttf") == s3::KeyCheck::Invalid);
    // Extensions: .ttf / .otf, lower case.
    TEST_ASSERT_TRUE(fk("medium.TTF") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("medium.woff2") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("medium.bin") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("medium.js") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("medium.ttf.tmp") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk(".ttf") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("ttf") == s3::KeyCheck::Invalid);
    // Shape and characters.
    TEST_ASSERT_TRUE(fk("") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("a..b.ttf") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("my font.ttf") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("a\x01.ttf") == s3::KeyCheck::Invalid);
    TEST_ASSERT_TRUE(fk("caf\xc3\xa9.ttf") == s3::KeyCheck::Invalid);
    // Length: MEDIA_KEY_MAX (96), not app's 63; no name_max per component.
    const std::string k96 = std::string(92, 'a') + ".ttf";
    TEST_ASSERT_EQUAL(MEDIA_KEY_MAX, k96.size());
    TEST_ASSERT_TRUE(fk(k96.c_str()) == s3::KeyCheck::Ok);
    const std::string k97 = std::string(93, 'a') + ".ttf";
    TEST_ASSERT_TRUE(fk(k97.c_str()) == s3::KeyCheck::Invalid);
}

void test_media_write_refusals() {
    const s3::Bucket& f = bucket("fonts");
    TEST_ASSERT_EQUAL(0, s3::writeRefusal(f, "medium.ttf", 10).status);
    s3::HttpError e = s3::writeRefusal(f, "medium.txt", 10);
    TEST_ASSERT_EQUAL(400, e.status);
    TEST_ASSERT_EQUAL_STRING("InvalidArgument", e.code);
    TEST_ASSERT_TRUE(strstr(e.msg, ".ttf") != nullptr);
    e = s3::writeRefusal(f, "a/b.ttf", 7);
    TEST_ASSERT_EQUAL(400, e.status);
    TEST_ASSERT_EQUAL_STRING("InvalidArgument", e.code);
    const std::string k97 = std::string(93, 'a') + ".ttf";
    e = s3::writeRefusal(f, k97.c_str(), k97.size());
    TEST_ASSERT_EQUAL(400, e.status);
    TEST_ASSERT_EQUAL_STRING("KeyTooLongError", e.code);
    // The LittleFS buckets keep their answers.
    e = s3::writeRefusal(bucket("web"), "a.js", 4);
    TEST_ASSERT_EQUAL(403, e.status);
    TEST_ASSERT_EQUAL_STRING("AccessDenied", e.code);
    e = s3::writeRefusal(bucket("app"), "js_bytecode_meta.json", 21);
    TEST_ASSERT_EQUAL(403, e.status);
    e = s3::writeRefusal(bucket("app"), "www/a.js", 8);
    TEST_ASSERT_EQUAL(400, e.status);
    TEST_ASSERT_EQUAL_STRING("InvalidArgument", e.code);
    const std::string a64 = std::string(61, 'a') + ".js";
    e = s3::writeRefusal(bucket("app"), a64.c_str(), a64.size());
    TEST_ASSERT_EQUAL_STRING("KeyTooLongError", e.code);
    e = s3::writeRefusal(bucket("app"), "a.ttf", 5);
    TEST_ASSERT_EQUAL(400, e.status);
    TEST_ASSERT_TRUE(strstr(e.msg, ".json") != nullptr);
}

void test_media_read_keys() {
    using R = s3::MediaReadKey;
    TEST_ASSERT_TRUE(s3::checkMediaReadKey("medium.ttf", 10) == R::Ok);
    TEST_ASSERT_TRUE(s3::checkMediaReadKey("anything.bin", 12) == R::Ok);  // the store answers 404
    TEST_ASSERT_TRUE(s3::checkMediaReadKey("a/b.ttf", 7) == R::Missing);
    TEST_ASSERT_TRUE(s3::checkMediaReadKey("", 0) == R::Malformed);
    TEST_ASSERT_TRUE(s3::checkMediaReadKey("../x.ttf", 8) == R::Malformed);
    TEST_ASSERT_TRUE(s3::checkMediaReadKey("a//b", 4) == R::Missing);
    TEST_ASSERT_TRUE(s3::checkMediaReadKey("a\x01.ttf", 6) == R::Malformed);
    const std::string k96(96, 'a');  // longer than LittleFS' name_max: fine here
    TEST_ASSERT_TRUE(s3::checkMediaReadKey(k96.c_str(), k96.size()) == R::Ok);
    const std::string k97(97, 'a');
    TEST_ASSERT_TRUE(s3::checkMediaReadKey(k97.c_str(), k97.size()) == R::Malformed);
    // Media keys never collide with LittleFS paths.
    TEST_ASSERT_NULL(s3::keyOwner(bucket("fonts"), "www/a", 5));
}

void test_media_size_limits() {
    TEST_ASSERT_EQUAL(64u * 1024, s3::objectSizeLimit(bucket("app")));
    TEST_ASSERT_EQUAL(2u * 1024 * 1024, s3::objectSizeLimit(bucket("fonts")));
    TEST_ASSERT_EQUAL_STRING("object larger than 64 KiB", s3::tooLargeMessage(bucket("app")));
    TEST_ASSERT_EQUAL_STRING("object larger than 2 MiB", s3::tooLargeMessage(bucket("fonts")));
}

void test_put_length() {
    const s3::Bucket& f = bucket("fonts");
    const s3::Bucket& a = bucket("app");
    // Plain body: Content-Length.
    s3::PutLength r = s3::putLength(f, false, nullptr, true, 300000);
    TEST_ASSERT_EQUAL(0, r.error.status);
    TEST_ASSERT_TRUE(r.known);
    TEST_ASSERT_EQUAL(300000u, r.expected);
    r = s3::putLength(f, false, nullptr, true, 0);
    TEST_ASSERT_TRUE(r.known);
    TEST_ASSERT_EQUAL(0u, r.expected);
    // aws-chunked: the decoded length, never the encoded Content-Length.
    r = s3::putLength(f, true, "123456", true, 130000);
    TEST_ASSERT_TRUE(r.known);
    TEST_ASSERT_EQUAL(123456u, r.expected);
    r = s3::putLength(a, true, "100", true, 300);
    TEST_ASSERT_EQUAL(100u, r.expected);
    // aws-chunked without a decoded length: media 411, app streams capped.
    r = s3::putLength(f, true, nullptr, true, 1000);
    TEST_ASSERT_EQUAL(411, r.error.status);
    TEST_ASSERT_EQUAL_STRING("MissingContentLength", r.error.code);
    r = s3::putLength(a, true, nullptr, true, 1000);
    TEST_ASSERT_EQUAL(0, r.error.status);
    TEST_ASSERT_FALSE(r.known);
    // No length at all: 411 everywhere.
    TEST_ASSERT_EQUAL(411, s3::putLength(f, false, nullptr, false, 0).error.status);
    TEST_ASSERT_EQUAL(411, s3::putLength(a, false, nullptr, false, 0).error.status);
    TEST_ASSERT_EQUAL(411, s3::putLength(a, true, nullptr, false, 0).error.status);
    // Limits per bucket.
    TEST_ASSERT_EQUAL(0, s3::putLength(f, false, nullptr, true, 2u * 1024 * 1024).error.status);
    r = s3::putLength(f, false, nullptr, true, 2u * 1024 * 1024 + 1);
    TEST_ASSERT_EQUAL(413, r.error.status);
    TEST_ASSERT_EQUAL_STRING("EntityTooLarge", r.error.code);
    TEST_ASSERT_EQUAL_STRING("object larger than 2 MiB", r.error.msg);
    TEST_ASSERT_EQUAL(413, s3::putLength(f, true, "2097153", true, 3000000).error.status);
    TEST_ASSERT_EQUAL(413, s3::putLength(a, false, nullptr, true, 65537).error.status);
    TEST_ASSERT_EQUAL(0, s3::putLength(a, false, nullptr, true, 65536).error.status);
    TEST_ASSERT_EQUAL(413, s3::putLength(f, true, "99999999999999999999", true, 1).error.status);  // saturates
    TEST_ASSERT_EQUAL(413, s3::putLength(f, false, nullptr, true, 0x100000000ull).error.status);
    // Malformed decoded length.
    TEST_ASSERT_EQUAL(400, s3::putLength(f, true, "", true, 1).error.status);
    TEST_ASSERT_EQUAL(400, s3::putLength(f, true, "12x", true, 1).error.status);
    TEST_ASSERT_EQUAL(400, s3::putLength(f, true, "-1", true, 1).error.status);
    TEST_ASSERT_EQUAL(5u, s3::putLength(f, true, " 5 ", true, 1).expected);
}

void test_media_error_mapping() {
    using s3::MediaOp;
    auto st = [](media_err_t e, MediaOp op) { return s3::mediaError(e, op).status; };
    auto code = [](media_err_t e, MediaOp op) { return s3::mediaError(e, op).code; };
    for (MediaOp op : {MediaOp::Read, MediaOp::Write, MediaOp::Delete}) {
        TEST_ASSERT_EQUAL(0, st(MEDIA_OK, op));
        TEST_ASSERT_EQUAL(503, st(MEDIA_ERR_UNAVAILABLE, op));
        TEST_ASSERT_EQUAL_STRING("ServiceUnavailable", code(MEDIA_ERR_UNAVAILABLE, op));
        TEST_ASSERT_EQUAL(503, st(MEDIA_ERR_BUSY, op));
        TEST_ASSERT_EQUAL_STRING("SlowDown", code(MEDIA_ERR_BUSY, op));
        TEST_ASSERT_EQUAL(507, st(MEDIA_ERR_NO_SPACE, op));
        TEST_ASSERT_EQUAL_STRING("InsufficientStorage", code(MEDIA_ERR_NO_SPACE, op));
        TEST_ASSERT_EQUAL(500, st(MEDIA_ERR_IO, op));
        TEST_ASSERT_EQUAL(400, st(MEDIA_ERR_SIZE, op));
        TEST_ASSERT_EQUAL(500, st(static_cast<media_err_t>(99), op));
    }
    TEST_ASSERT_EQUAL(404, st(MEDIA_ERR_NOT_FOUND, MediaOp::Read));
    TEST_ASSERT_EQUAL_STRING("NoSuchKey", code(MEDIA_ERR_NOT_FOUND, MediaOp::Read));
    TEST_ASSERT_EQUAL(0, st(MEDIA_ERR_NOT_FOUND, MediaOp::Delete));  // S3: 204 for a missing key
    TEST_ASSERT_EQUAL(500, st(MEDIA_ERR_NOT_FOUND, MediaOp::Write));
    TEST_ASSERT_EQUAL(400, st(MEDIA_ERR_INVALID, MediaOp::Write));
    TEST_ASSERT_EQUAL_STRING("InvalidArgument", code(MEDIA_ERR_INVALID, MediaOp::Write));
    TEST_ASSERT_EQUAL(400, st(MEDIA_ERR_INVALID, MediaOp::Delete));
    TEST_ASSERT_EQUAL(404, st(MEDIA_ERR_INVALID, MediaOp::Read));
}

void test_no_space_message() {
    char out[112];
    s3::formatNoSpaceMessage(1234567, 65536, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("object of 1234567 bytes does not fit: 65536 bytes free, 0 reclaimable once released",
                             out);
    s3::formatNoSpaceMessage(4294967295u, 0, 131072, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING(
        "object of 4294967295 bytes does not fit: 0 bytes free, 131072 reclaimable once released", out);
    char small[12];
    s3::formatNoSpaceMessage(1, 2, 3, small, sizeof small);
    TEST_ASSERT_EQUAL_STRING("object of 1", small);
    s3::formatNoSpaceMessage(1, 2, 3, small, 1);
    TEST_ASSERT_EQUAL_STRING("", small);
}

void test_md5_etag_and_mtime() {
    // MD5("") = d41d8cd98f00b204e9800998ecf8427e
    const uint8_t md5[16] = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
                             0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
    char etag[35];
    s3::formatMd5ETag(md5, etag);
    TEST_ASSERT_EQUAL_STRING("\"d41d8cd98f00b204e9800998ecf8427e\"", etag);
    TEST_ASSERT_EQUAL(0u, s3::mediaMtime(false, 1800000000));
    TEST_ASSERT_EQUAL(1800000000u, s3::mediaMtime(true, 1800000000));
    TEST_ASSERT_EQUAL(0u, s3::mediaMtime(true, -5));
    TEST_ASSERT_EQUAL(0xFFFFFFFFu, s3::mediaMtime(true, 0x1FFFFFFFFll));
}

void test_media_usage_headers() {
    TEST_ASSERT_EQUAL_STRING("x-fb-media-total", s3::kMediaTotalHeader);
    TEST_ASSERT_EQUAL_STRING("x-fb-media-used", s3::kMediaUsedHeader);
    TEST_ASSERT_EQUAL_STRING("x-fb-media-free", s3::kMediaFreeHeader);
    TEST_ASSERT_EQUAL_STRING("x-fb-media-reclaimable", s3::kMediaReclaimableHeader);
}

// Which S3 requests need no signature.
static void test_anonymous_access() {
    TEST_ASSERT_TRUE(s3::anonymousAllowed(nullptr, true));    // ListBuckets, unknown bucket
    TEST_ASSERT_FALSE(s3::anonymousAllowed(nullptr, false));
    TEST_ASSERT_FALSE(s3::anonymousAllowed(&bucket("app"), true));   // config.json holds secrets
    TEST_ASSERT_FALSE(s3::anonymousAllowed(&bucket("app"), false));
    for (const char* b : {"web", "examples", "fonts"}) {
        TEST_ASSERT_TRUE(s3::anonymousAllowed(&bucket(b), true));
        TEST_ASSERT_FALSE(s3::anonymousAllowed(&bucket(b), false));
    }
}


int main() {
    UNITY_BEGIN();
    RUN_TEST(test_bucket_table);
    RUN_TEST(test_list_buckets_xml_matches_table);
    RUN_TEST(test_parse_url);
    RUN_TEST(test_key_owner);
    RUN_TEST(test_bucket_paths);
    RUN_TEST(test_write_rules_per_bucket);
    RUN_TEST(test_write_key_accepts_plain_paths);
    RUN_TEST(test_write_key_rejects_traversal_and_shape);
    RUN_TEST(test_write_key_extensions);
    RUN_TEST(test_write_key_other_buckets_and_protected);
    RUN_TEST(test_write_key_lengths);
    RUN_TEST(test_read_key);
    RUN_TEST(test_content_types);
    RUN_TEST(test_xml_escape);
    RUN_TEST(test_url_encode);
    RUN_TEST(test_hex_token_roundtrip);
    RUN_TEST(test_put_uint);
    RUN_TEST(test_base64);
    RUN_TEST(test_crc32_b64);
    RUN_TEST(test_dates);
    RUN_TEST(test_etag);
    RUN_TEST(test_rollup);
    RUN_TEST(test_dir_action);
    RUN_TEST(test_subtree_pruning);
    RUN_TEST(test_chunked_unsigned_trailer);
    RUN_TEST(test_chunked_signed);
    RUN_TEST(test_chunked_signed_trailer_and_case);
    RUN_TEST(test_chunked_empty_body);
    RUN_TEST(test_chunked_errors);
    RUN_TEST(test_json_validator);
    RUN_TEST(test_json_validator_bounds);
    RUN_TEST(test_chunked_signatures);
    RUN_TEST(test_anonymous_access);
    RUN_TEST(test_media_write_keys);
    RUN_TEST(test_media_write_refusals);
    RUN_TEST(test_media_read_keys);
    RUN_TEST(test_media_size_limits);
    RUN_TEST(test_put_length);
    RUN_TEST(test_media_error_mapping);
    RUN_TEST(test_no_space_message);
    RUN_TEST(test_md5_etag_and_mtime);
    RUN_TEST(test_media_usage_headers);
    return UNITY_END();
}
