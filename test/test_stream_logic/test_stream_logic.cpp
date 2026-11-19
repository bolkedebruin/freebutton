//
// Native unit tests for the download rules (src/core/StreamLogic.h).
//
#include <unity.h>
#include <cstring>

#include "../../src/core/StreamLogic.h"

void setUp() {}
void tearDown() {}

static void test_first_download_always_starts() {
    TEST_ASSERT_TRUE(stream::mayStart(0, 0));
    TEST_ASSERT_TRUE(stream::mayStart(0, 100 * 1024));
}

static void test_second_download_needs_heap() {
    TEST_ASSERT_TRUE(stream::mayStart(1, stream::kSecondStreamHeap));
    TEST_ASSERT_FALSE(stream::mayStart(1, stream::kSecondStreamHeap - 1));
}

static void test_no_download_beyond_the_limit() {
    TEST_ASSERT_FALSE(stream::mayStart(stream::kMaxStreams, 1024 * 1024));
}

static void test_stall_survives_clock_wrap() {
    TEST_ASSERT_FALSE(stream::stalled(5000, 1000));
    TEST_ASSERT_TRUE(stream::stalled(1000 + stream::kStallMs, 1000));
    const uint32_t at = 0xFFFFF000u;
    TEST_ASSERT_FALSE(stream::stalled(at + 6000, at));
    TEST_ASSERT_TRUE(stream::stalled(at + stream::kStallMs, at));
}

static void test_piece_is_at_most_one_segment() {
    TEST_ASSERT_EQUAL(stream::kPieceMax, stream::pieceSize(2880, 100000));
    TEST_ASSERT_EQUAL(700, stream::pieceSize(700, 100000));
    TEST_ASSERT_EQUAL(12, stream::pieceSize(2880, 12));
    TEST_ASSERT_EQUAL(0, stream::pieceSize(0, 12));
    TEST_ASSERT_EQUAL(0, stream::pieceSize(2880, 0));
}

static const char* type(const char* p) { return stream::staticContentType(p, strlen(p)); }

static void test_content_types() {
    TEST_ASSERT_EQUAL_STRING("application/javascript", type("/www/assets/ide-DXPoTqIV.js.gz"));
    TEST_ASSERT_EQUAL_STRING("application/wasm", type("/www/assets/fb-emulator.wasm.gz"));
    TEST_ASSERT_EQUAL_STRING("application/wasm", type("/www/assets/mquickjs.wasm"));
    TEST_ASSERT_EQUAL_STRING("text/css", type("/www/assets/index-C4K7GXCf.css.gz"));
    TEST_ASSERT_EQUAL_STRING("text/html", type("/www/index.html.gz"));
    TEST_ASSERT_EQUAL_STRING("font/woff2", type("/www/fonts/inter.woff2"));
    TEST_ASSERT_EQUAL_STRING("text/plain", type("/www/assets/fb-emulator.stamp.txt.gz"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", type("/www/assets/x.gz"));
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", type("/www/unknown"));
    TEST_ASSERT_EQUAL_STRING("application/json", type("/www/assets/ide-DXPoTqIV.js.map"));
    TEST_ASSERT_EQUAL_STRING("font/ttf", type("/www/fonts/medium.ttf"));
    TEST_ASSERT_EQUAL_STRING("font/otf", type("/www/fonts/medium.otf"));  // from the S3 table
}

static const char* cache(const char* url) { return stream::cacheControlFor(url, strlen(url)); }

static void test_cache_control() {
    static const char kForGood[] = "public, max-age=31536000, immutable";
    static const char kWeek[] = "public, max-age=604800";
    // a content hash in the name: kept for good
    TEST_ASSERT_EQUAL_STRING(kForGood, cache("/assets/index-DSSHvBwi.js"));
    TEST_ASSERT_EQUAL_STRING(kForGood, cache("/assets/index-C4K7GXCf.css"));
    TEST_ASSERT_EQUAL_STRING(kForGood, cache("/assets/ide-a1b2c3d4.js"));
    TEST_ASSERT_EQUAL_STRING(kForGood, cache("/assets/a-b_c-1234.js"));
    // no hash: checked on every load
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/assets/fb-emulator.wasm"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/assets/fb-emulator.stamp.txt"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/index.html"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache(""));
    // eight lower-case letters are a word, not a hash
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/assets/mquickjs-compiler.js"));
    // the hash must be exactly eight of [A-Za-z0-9_-] after a '-', before the last '.'
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/assets/index-DSSHvBw.js"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/assets/index-DSS.vBwi.js"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/assets/index-DSS$vBwi.js"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/assets/index-DSSHvBwi"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/assets/DSSHvBwi.js"));
    // only below /assets/
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/index-DSSHvBwi.js"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/other/index-DSSHvBwi.js"));
    // the IDE's web fonts: a week
    TEST_ASSERT_EQUAL_STRING(kWeek, cache("/fonts/inter.woff2"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/fonts/inter.woff"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/fonts/medium.ttf"));
    TEST_ASSERT_EQUAL_STRING("no-cache", cache("/assets/inter.woff2"));
    TEST_ASSERT_TRUE(stream::isHashedAssetUrl("/assets/index-DSSHvBwi.js", 25));
    TEST_ASSERT_FALSE(stream::isHashedAssetUrl("/assets/index-DSSHvBwi.js", 22));  // not NUL-bound: the length counts
    TEST_ASSERT_TRUE(stream::isWebFontUrl("/fonts/inter.woff2", 18));
}

static bool path(const char* url, char* out, size_t size = stream::kStaticPathMax + 1) {
    return stream::staticPath(url, strlen(url), out, size);
}

static void test_static_paths() {
    char out[stream::kStaticPathMax + 1];
    TEST_ASSERT_TRUE(path("/", out));
    TEST_ASSERT_EQUAL_STRING("/www/index.html", out);
    TEST_ASSERT_TRUE(path("/assets/ide-DXPoTqIV.js", out));
    TEST_ASSERT_EQUAL_STRING("/www/assets/ide-DXPoTqIV.js", out);
    TEST_ASSERT_TRUE(path("/docs/", out));
    TEST_ASSERT_EQUAL_STRING("/www/docs/index.html", out);
    TEST_ASSERT_TRUE(path("/a..b/c..", out));  // dots inside a name are a name
    TEST_ASSERT_TRUE(path("/.well-known/x", out));
}

static void test_static_paths_refused() {
    char out[stream::kStaticPathMax + 1];
    TEST_ASSERT_FALSE(path("", out));
    TEST_ASSERT_FALSE(path("assets/x.js", out));
    TEST_ASSERT_FALSE(path("/..", out));
    TEST_ASSERT_FALSE(path("/../config.json", out));
    TEST_ASSERT_FALSE(path("/assets/../../config.json", out));
    TEST_ASSERT_FALSE(path("/assets/..", out));
    TEST_ASSERT_FALSE(path("/a\\b", out));
    TEST_ASSERT_FALSE(path("/a\nb", out));
    char longUrl[200];
    memset(longUrl, 'a', sizeof longUrl - 1);
    longUrl[0] = '/';
    longUrl[sizeof longUrl - 1] = '\0';
    TEST_ASSERT_FALSE(path(longUrl, out));
}

static void test_static_path_leaves_room_for_gz() {
    char out[stream::kStaticPathMax + 1];
    char url[stream::kStaticPathMax];
    // the longest URL accepted: "/www" + url + ".gz" still fits
    const size_t len = stream::kStaticPathMax - 3 - strlen("/www");
    memset(url, 'a', len);
    url[0] = '/';
    url[len] = '\0';
    TEST_ASSERT_TRUE(path(url, out));
    TEST_ASSERT_TRUE(strlen(out) + 3 <= stream::kStaticPathMax);
    url[len] = 'a';
    url[len + 1] = '\0';
    TEST_ASSERT_FALSE(path(url, out));
}

static void test_etag() {
    char etag[11];
    stream::formatStaticETag(0xC5347D3Du, etag);
    TEST_ASSERT_EQUAL_STRING("\"c5347d3d\"", etag);
    stream::formatStaticETag(7, etag);
    TEST_ASSERT_EQUAL_STRING("\"00000007\"", etag);
}

static void test_if_none_match() {
    const char* e = "\"c5347d3d\"";
    TEST_ASSERT_TRUE(stream::etagMatches("\"c5347d3d\"", e));
    TEST_ASSERT_TRUE(stream::etagMatches("W/\"c5347d3d\"", e));
    TEST_ASSERT_TRUE(stream::etagMatches("\"aaaaaaaa\", \"c5347d3d\"", e));
    TEST_ASSERT_TRUE(stream::etagMatches("*", e));
    TEST_ASSERT_FALSE(stream::etagMatches("\"c5347d3e\"", e));
    TEST_ASSERT_FALSE(stream::etagMatches("\"c5347d3d0\"", e));
    TEST_ASSERT_FALSE(stream::etagMatches("", e));
    TEST_ASSERT_FALSE(stream::etagMatches(nullptr, e));
}

void test_too_slow_needs_a_full_window() {
    TEST_ASSERT_FALSE(stream::tooSlow(9999, 0, 0));
    TEST_ASSERT_TRUE(stream::tooSlow(10000, 0, 0));
    TEST_ASSERT_TRUE(stream::tooSlow(10000, 0, stream::kPaceMinBytes - 1));
    TEST_ASSERT_FALSE(stream::tooSlow(10000, 0, stream::kPaceMinBytes));
    // wrap of the millisecond clock
    TEST_ASSERT_TRUE(stream::tooSlow(6000, 0xFFFFFFFFu - 4000, 10));
    TEST_ASSERT_TRUE(stream::paceWindowOver(10000, 0));
    TEST_ASSERT_FALSE(stream::paceWindowOver(500, 0xFFFFFFFFu - 100));
}

void test_generated_piece() {
    // known length: room, one segment at most, never more than is left
    TEST_ASSERT_EQUAL(1440, stream::generatedPiece(2880, 100000, false));
    TEST_ASSERT_EQUAL(700, stream::generatedPiece(700, 100000, false));
    TEST_ASSERT_EQUAL(10, stream::generatedPiece(2880, 10, false));
    TEST_ASSERT_EQUAL(10, stream::generatedPiece(10, 10, false));   // the last bytes go out in any room
    TEST_ASSERT_EQUAL(0, stream::generatedPiece(10, 100000, false)); // too little room to bother
    TEST_ASSERT_EQUAL(0, stream::generatedPiece(0, 100, false));
    // chunked: the frame comes out of the room
    TEST_ASSERT_EQUAL(1440, stream::generatedPiece(2880, SIZE_MAX, true));
    TEST_ASSERT_EQUAL(1440 - stream::kChunkFrame, stream::generatedPiece(1440, SIZE_MAX, true));
    TEST_ASSERT_EQUAL(0, stream::generatedPiece(stream::kChunkFrame, SIZE_MAX, true));
    TEST_ASSERT_EQUAL(0, stream::generatedPiece(stream::kChunkFrame + 20, SIZE_MAX, true));
}

void test_chunk_head() {
    char h[stream::kChunkHead + 1] = {};
    stream::chunkHead(1440, h);
    TEST_ASSERT_EQUAL_STRING("5a0\r\n", h);
    stream::chunkHead(7, h);
    TEST_ASSERT_EQUAL_STRING("007\r\n", h);
    TEST_ASSERT_TRUE(stream::kPieceMax <= 0xfff);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_download_always_starts);
    RUN_TEST(test_second_download_needs_heap);
    RUN_TEST(test_no_download_beyond_the_limit);
    RUN_TEST(test_stall_survives_clock_wrap);
    RUN_TEST(test_piece_is_at_most_one_segment);
    RUN_TEST(test_content_types);
    RUN_TEST(test_cache_control);
    RUN_TEST(test_static_paths);
    RUN_TEST(test_static_paths_refused);
    RUN_TEST(test_static_path_leaves_room_for_gz);
    RUN_TEST(test_etag);
    RUN_TEST(test_if_none_match);
    RUN_TEST(test_too_slow_needs_a_full_window);
    RUN_TEST(test_generated_piece);
    RUN_TEST(test_chunk_head);
    return UNITY_END();
}
