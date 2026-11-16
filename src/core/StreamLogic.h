//
// Pure rules of the download path (StreamResponse, StaticFiles): no Arduino,
// no lwIP, so the native tests cover them (test/test_stream_logic).
//
#ifndef FREEBUTTON_STREAMLOGIC_H
#define FREEBUTTON_STREAMLOGIC_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace stream {

// ---------------------------------------------------------------------------
// Admission: how many downloads may send at the same time
// ---------------------------------------------------------------------------
//
// A download that is sending holds, outside this code: the TCP stack's copy
// of the unacknowledged bytes (up to the send buffer, 2 segments) and the
// Wi-Fi driver's copy of each frame on its way out; measured, about 9 KB at
// its peak. A download that waits holds only its request (under 1 KB).
//
// Two senders are about twice as fast as one (each is paced by its own
// 2-segment window), and cost about 9 KB more at the peak. So the second
// slot is given only on a device with room for it: free heap, measured while
// the first download is already sending. btn_a00001 idles at 38 KB and so
// sends one at a time; a device with a lighter configuration sends two.

constexpr uint8_t kMaxStreams = 2;
constexpr uint32_t kSecondStreamHeap = 40 * 1024;

inline bool mayStart(uint8_t active, uint32_t freeHeap) {
    if (active == 0) return true;  // always: nothing else would ever free memory for it
    return active < kMaxStreams && freeHeap >= kSecondStreamHeap;
}

// A download that holds a slot and has neither sent nor had anything
// acknowledged for this long gives its slot up (and is closed when its
// connection next shows life): a client that went silent must not keep the
// waiting downloads out. The server library closes a connection whose data
// stays unacknowledged for 5 s; this catches the cases it does not see, such
// as a client that half-closed before the first byte went out.
constexpr uint32_t kStallMs = 20 * 1000;

inline bool stalled(uint32_t nowMs, uint32_t lastProgressMs) {
    return static_cast<uint32_t>(nowMs - lastProgressMs) >= kStallMs;  // wrap-safe
}

// ---------------------------------------------------------------------------
// Piece size for a file that is not mapped (LittleFS)
// ---------------------------------------------------------------------------
//
// Read straight into a scratch block that lives only for the call and is
// handed to the TCP stack at once. One segment at most: the block is the only
// buffer of ours a download ever needs.

constexpr size_t kPieceMax = 1440;

inline size_t pieceSize(size_t tcpRoom, size_t remaining) {
    size_t n = tcpRoom < remaining ? tcpRoom : remaining;
    return n > kPieceMax ? kPieceMax : n;
}

// ---------------------------------------------------------------------------
// Mapped files (media store)
// ---------------------------------------------------------------------------
//
// A mapped file is copied once, by the TCP stack, from the mapping into the
// segment it sends. Handing it over by reference instead (tcp_write without
// TCP_WRITE_FLAG_COPY) was built and measured, and is not used: the stack
// then makes a two-part segment (header + reference), and the ESP network
// interface copies every such segment into a fresh block before the Wi-Fi
// driver copies it again (esp_netif wlanif.c, low_level_output). It saved no
// memory (a 1.3 MB font: 46 KB lowest free heap either way; after six
// interrupted downloads 38 KB by reference, 43 KB copied), and the mapping
// would have to outlive the connection for retransmissions.

// ---------------------------------------------------------------------------
// The web IDE's files (LittleFS /www)
// ---------------------------------------------------------------------------

inline bool endsWith(const char* s, size_t len, const char* suffix) {
    const size_t n = strlen(suffix);
    return len >= n && memcmp(s + len - n, suffix, n) == 0;
}

// Content type from the name; a trailing ".gz" is not part of the name.
inline const char* staticContentType(const char* path, size_t len) {
    if (endsWith(path, len, ".gz")) len -= 3;
    if (endsWith(path, len, ".js") || endsWith(path, len, ".mjs")) return "application/javascript";
    if (endsWith(path, len, ".css")) return "text/css";
    if (endsWith(path, len, ".html") || endsWith(path, len, ".htm")) return "text/html";
    if (endsWith(path, len, ".wasm")) return "application/wasm";
    if (endsWith(path, len, ".json") || endsWith(path, len, ".map")) return "application/json";
    if (endsWith(path, len, ".woff2")) return "font/woff2";
    if (endsWith(path, len, ".woff")) return "font/woff";
    if (endsWith(path, len, ".ttf")) return "font/ttf";
    if (endsWith(path, len, ".svg")) return "image/svg+xml";
    if (endsWith(path, len, ".png")) return "image/png";
    if (endsWith(path, len, ".ico")) return "image/x-icon";
    if (endsWith(path, len, ".txt")) return "text/plain";
    return "application/octet-stream";
}

constexpr size_t kStaticPathMax = 127;  // "/www" + URL + "index.html" + ".gz"
inline constexpr const char* kStaticRoot = "/www";
inline constexpr const char* kStaticDefault = "index.html";

// URL path -> file path below /www ("/" and ".../" get index.html). False
// when the URL cannot name a file there: too long, not absolute, a ".."
// segment, a backslash or a control character.
inline bool staticPath(const char* url, size_t urlLen, char* out, size_t outSize) {
    if (urlLen == 0 || url[0] != '/') return false;
    for (size_t i = 0; i < urlLen; ++i) {
        const unsigned char c = static_cast<unsigned char>(url[i]);
        if (c < 0x20 || c == 0x7f || c == '\\') return false;
        // a ".." segment (i > 0 here: url[0] is '/')
        if (c == '.' && url[i - 1] == '/' && i + 1 < urlLen && url[i + 1] == '.' &&
            (i + 2 == urlLen || url[i + 2] == '/')) {
            return false;
        }
    }
    const bool dir = url[urlLen - 1] == '/';
    const size_t need = strlen(kStaticRoot) + urlLen + (dir ? strlen(kStaticDefault) : 0);
    if (need > kStaticPathMax - 3 || need + 4 > outSize) return false;  // room for ".gz"
    size_t n = strlen(kStaticRoot);
    memcpy(out, kStaticRoot, n);
    memcpy(out + n, url, urlLen);
    n += urlLen;
    if (dir) {
        memcpy(out + n, kStaticDefault, strlen(kStaticDefault));
        n += strlen(kStaticDefault);
    }
    out[n] = '\0';
    return true;
}

// ETag of a static file: for a .gz the CRC-32 of its content (the gzip
// trailer), else its modification time. out: 11 bytes.
inline void formatStaticETag(uint32_t value, char out[11]) {
    snprintf(out, 11, "\"%08lx\"", static_cast<unsigned long>(value));
}

// If-None-Match against our ETag: exact, in a list, weak ("W/") or "*".
inline bool etagMatches(const char* ifNoneMatch, const char* etag) {
    if (!ifNoneMatch || !etag || !*etag) return false;
    const size_t n = strlen(etag);
    for (const char* p = ifNoneMatch; *p;) {
        while (*p == ' ' || *p == ',' || *p == '\t') ++p;
        if (*p == '*') return true;
        if (p[0] == 'W' && p[1] == '/') p += 2;
        if (strncmp(p, etag, n) == 0 && (p[n] == '\0' || p[n] == ',' || p[n] == ' ' || p[n] == '\t')) return true;
        while (*p && *p != ',') ++p;
    }
    return false;
}

}  // namespace stream

#endif  // FREEBUTTON_STREAMLOGIC_H
