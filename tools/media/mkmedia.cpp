// mkmedia: build or inspect a media partition image (docs/MEDIA-STORE.md)
// with the firmware's own store core (src/media/media_core.c) on a
// file-backed flash.
//
//   mkmedia OUT.bin --size 0x500000 [--mtime SECS] BUCKET/KEY=PATH ...
//   mkmedia --list IMAGE.bin
//
// Buckets: fonts, images, render. --list mounts the image, lists every
// entry and checks each file's MD5 against the one stored (exit 1 on a
// mismatch or an unreadable image). Build: tools/media/build.sh.
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/media/media_core.h"

// ------------------------------------------------------------------ MD5

namespace {

struct Md5 {
    uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    uint64_t len = 0;
    uint8_t buf[64];
    size_t fill = 0;

    static uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

    void block(const uint8_t *p) {
        static const uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
        uint32_t m[16];
        for (int i = 0; i < 16; i++)
            m[i] = p[i * 4] | p[i * 4 + 1] << 8 | p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int gi;
            if (i < 16) f = (b & c) | (~b & d), gi = i;
            else if (i < 32) f = (d & b) | (~d & c), gi = (5 * i + 1) % 16;
            else if (i < 48) f = b ^ c ^ d, gi = (3 * i + 5) % 16;
            else f = c ^ (b | ~d), gi = (7 * i) % 16;
            const uint32_t t = d;
            d = c;
            c = b;
            b = b + rol(a + f + K[i] + m[gi], R[i]);
            a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d;
    }

    void update(const uint8_t *p, size_t n) {
        len += n;
        while (n) {
            const size_t k = std::min(n, sizeof buf - fill);
            memcpy(buf + fill, p, k);
            fill += k, p += k, n -= k;
            if (fill == sizeof buf) block(buf), fill = 0;
        }
    }

    void final(uint8_t out[16]) {
        const uint64_t bits = len * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (fill != 56) update(&zero, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (8 * i));
        update(l, 8);
        for (int i = 0; i < 16; i++) out[i] = (uint8_t)(h[i / 4] >> (8 * (i % 4)));
    }
};

std::string hex(const uint8_t *p, size_t n) {
    std::string s;
    char b[3];
    for (size_t i = 0; i < n; i++) snprintf(b, sizeof b, "%02x", p[i]), s += b;
    return s;
}

// ------------------------------------------------------ file-backed flash

struct FileFlash {
    FILE *f = nullptr;
    media_flash_t flash{};
};

int ff_read(void *c, uint32_t off, void *dst, uint32_t len) {
    FILE *f = static_cast<FileFlash *>(c)->f;
    return fseek(f, (long)off, SEEK_SET) == 0 && fread(dst, 1, len, f) == len ? 0 : -1;
}

int ff_write(void *c, uint32_t off, const void *src, uint32_t len) {
    // NOR: programming only clears bits
    std::vector<uint8_t> cur(len);
    if (ff_read(c, off, cur.data(), len)) return -1;
    const uint8_t *s = static_cast<const uint8_t *>(src);
    for (uint32_t i = 0; i < len; i++) cur[i] &= s[i];
    FILE *f = static_cast<FileFlash *>(c)->f;
    return fseek(f, (long)off, SEEK_SET) == 0 && fwrite(cur.data(), 1, len, f) == len ? 0 : -1;
}

int ff_erase(void *c, uint32_t off, uint32_t len) {
    std::vector<uint8_t> ff(len, 0xFF);
    FILE *f = static_cast<FileFlash *>(c)->f;
    return fseek(f, (long)off, SEEK_SET) == 0 && fwrite(ff.data(), 1, len, f) == len ? 0 : -1;
}

int ff_map(void *c, const uint8_t *pages, uint32_t n, const void **out, void **handle) {
    uint8_t *m = static_cast<uint8_t *>(malloc((size_t)n * MEDIA_PAGE_SIZE));
    if (!m) return -1;
    for (uint32_t i = 0; i < n; i++)
        if (ff_read(c, pages[i] * MEDIA_PAGE_SIZE, m + (size_t)i * MEDIA_PAGE_SIZE, MEDIA_PAGE_SIZE)) {
            free(m);
            return -1;
        }
    *out = m;
    *handle = m;
    return 0;
}

void ff_unmap(void *, void *handle) { free(handle); }

bool open_flash(FileFlash &ff, const char *path, uint32_t size, bool create) {
    ff.f = fopen(path, create ? "w+b" : "r+b");
    if (!ff.f) {
        fprintf(stderr, "mkmedia: %s: %s\n", path, strerror(errno));
        return false;
    }
    if (create) {
        std::vector<uint8_t> blank(MEDIA_PAGE_SIZE, 0xFF);
        for (uint32_t off = 0; off < size; off += MEDIA_PAGE_SIZE)
            if (fwrite(blank.data(), 1, blank.size(), ff.f) != blank.size()) return false;
    } else {
        fseek(ff.f, 0, SEEK_END);
        const long n = ftell(ff.f);
        if (n <= 0 || n % MEDIA_PAGE_SIZE) {
            fprintf(stderr, "mkmedia: %s: size %ld is not a multiple of 64 KB\n", path, n);
            return false;
        }
        size = (uint32_t)n;
    }
    ff.flash.ctx = &ff;
    ff.flash.size = size;
    ff.flash.read = ff_read;
    ff.flash.write = ff_write;
    ff.flash.erase = ff_erase;
    ff.flash.map = ff_map;
    ff.flash.unmap = ff_unmap;
    return true;
}

const char *const kBuckets[MEDIA_BUCKET_COUNT] = {"fonts", "images", "render"};

const char *err_name(media_err_t e) {
    switch (e) {
    case MEDIA_OK: return "ok";
    case MEDIA_ERR_UNAVAILABLE: return "unavailable";
    case MEDIA_ERR_NOT_FOUND: return "not found";
    case MEDIA_ERR_NO_SPACE: return "no space";
    case MEDIA_ERR_INVALID: return "invalid (bucket, key or size)";
    case MEDIA_ERR_BUSY: return "busy";
    case MEDIA_ERR_SIZE: return "size mismatch";
    case MEDIA_ERR_IO: return "i/o error";
    }
    return "?";
}

int usage() {
    fprintf(stderr,
            "usage: mkmedia OUT.bin --size BYTES [--mtime SECS] BUCKET/KEY=PATH ...\n"
            "       mkmedia --list IMAGE.bin\n"
            "buckets: fonts, images, render\n");
    return 2;
}

bool add_file(const std::string &spec, long mtime_override) {
    const size_t eq = spec.find('=');
    const size_t slash = spec.find('/');
    if (eq == std::string::npos || slash == std::string::npos || slash > eq) {
        fprintf(stderr, "mkmedia: %s: expected BUCKET/KEY=PATH\n", spec.c_str());
        return false;
    }
    const std::string bucket = spec.substr(0, slash), key = spec.substr(slash + 1, eq - slash - 1),
                      path = spec.substr(eq + 1);
    int b = -1;
    for (int i = 0; i < MEDIA_BUCKET_COUNT; i++)
        if (bucket == kBuckets[i]) b = i;
    if (b < 0) {
        fprintf(stderr, "mkmedia: %s: unknown bucket '%s'\n", spec.c_str(), bucket.c_str());
        return false;
    }
    FILE *in = fopen(path.c_str(), "rb");
    struct stat st;
    if (!in || fstat(fileno(in), &st) != 0) {
        fprintf(stderr, "mkmedia: %s: %s\n", path.c_str(), strerror(errno));
        if (in) fclose(in);
        return false;
    }
    const uint32_t mtime = mtime_override >= 0 ? (uint32_t)mtime_override : (uint32_t)st.st_mtime;
    if ((unsigned long long)st.st_size > MEDIA_FILE_MAX) {
        fprintf(stderr, "mkmedia: %s: larger than %u bytes\n", path.c_str(), (unsigned)MEDIA_FILE_MAX);
        fclose(in);
        return false;
    }
    media_writer_t *w = nullptr;
    media_err_t e = media_put_begin((media_bucket_t)b, key.c_str(), (uint32_t)st.st_size, mtime, &w);
    Md5 md5;
    std::vector<uint8_t> buf(65536);
    size_t n;
    while (e == MEDIA_OK && (n = fread(buf.data(), 1, buf.size(), in)) > 0) {
        md5.update(buf.data(), n);
        e = media_put_write(w, buf.data(), (uint32_t)n);
    }
    fclose(in);
    uint8_t digest[16];
    md5.final(digest);
    if (e == MEDIA_OK) e = media_put_commit(w, digest);
    else if (w) media_put_abort(w);
    if (e != MEDIA_OK) {
        fprintf(stderr, "mkmedia: %s/%s: %s\n", bucket.c_str(), key.c_str(), err_name(e));
        return false;
    }
    printf("  %s/%s  %lld bytes  %s\n", bucket.c_str(), key.c_str(), (long long)st.st_size, hex(digest, 16).c_str());
    return true;
}

void print_usage_line() {
    media_usage_t u;
    media_usage(&u);
    printf("total %u KB, used %u KB, free %u KB\n", (unsigned)(u.total / 1024), (unsigned)(u.used / 1024),
           (unsigned)(u.free / 1024));
}

int list(const char *path) {
    FileFlash ff;
    if (!open_flash(ff, path, 0, false)) return 1;
    // a read-only look: refuse to format an unreadable image
    uint8_t hdr[2][4];
    if (ff_read(&ff, 0, hdr[0], 4) || ff_read(&ff, MEDIA_PAGE_SIZE, hdr[1], 4) ||
        (memcmp(hdr[0], "MDIR", 4) && memcmp(hdr[1], "MDIR", 4))) {
        fprintf(stderr, "mkmedia: %s: not a media image\n", path);
        return 1;
    }
    media_err_t e = media_mount(&ff.flash);
    if (e != MEDIA_OK) {
        fprintf(stderr, "mkmedia: %s: %s\n", path, err_name(e));
        return 1;
    }
    int bad = 0, count = 0;
    for (int b = 0; b < MEDIA_BUCKET_COUNT; b++) {
        std::string after;
        media_stat_t st;
        while ((e = media_next((media_bucket_t)b, after.c_str(), &st)) == MEDIA_OK) {
            media_file_t f{};
            Md5 md5;
            uint8_t digest[16];
            const media_err_t oe = media_open((media_bucket_t)b, st.key, &f);
            if (oe == MEDIA_OK) md5.update(f.data, f.size);
            md5.final(digest);
            const bool ok = oe == MEDIA_OK && f.size == st.size && memcmp(digest, st.md5, 16) == 0;
            media_close(&f);
            printf("%-8s %-40s %10u  mtime %10u  %s  %s\n", kBuckets[b], st.key, (unsigned)st.size,
                   (unsigned)st.mtime, hex(st.md5, 16).c_str(), ok ? "OK" : "BAD");
            bad += !ok;
            count++;
            after = st.key;
        }
        if (e != MEDIA_ERR_NOT_FOUND) {
            fprintf(stderr, "mkmedia: listing %s: %s\n", kBuckets[b], err_name(e));
            bad++;
        }
    }
    printf("%d entries, %d bad; ", count, bad);
    print_usage_line();
    media_unmount();
    fclose(ff.f);
    return bad ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--list") == 0) return list(argv[2]);
    if (argc < 2 || argv[1][0] == '-') return usage();
    const char *out = argv[1];
    unsigned long size = 0;
    long mtime = -1;
    std::vector<std::string> specs;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) size = strtoul(argv[++i], nullptr, 0);
        else if (strcmp(argv[i], "--mtime") == 0 && i + 1 < argc) mtime = strtol(argv[++i], nullptr, 0);
        else if (argv[i][0] == '-') return usage();
        else specs.emplace_back(argv[i]);
    }
    if (!size || size % MEDIA_PAGE_SIZE || size / MEDIA_PAGE_SIZE > MEDIA_MAX_PAGES) {
        fprintf(stderr, "mkmedia: --size must be a multiple of 64 KB, at most 16 MB\n");
        return 2;
    }
    FileFlash ff;
    if (!open_flash(ff, out, (uint32_t)size, true)) return 1;
    media_err_t e = media_mount(&ff.flash);
    if (e != MEDIA_OK) {
        fprintf(stderr, "mkmedia: %s: %s\n", out, err_name(e));
        return 1;
    }
    printf("%s (%lu bytes):\n", out, size);
    bool ok = true;
    for (const std::string &s : specs) ok = add_file(s, mtime) && ok;
    print_usage_line();
    media_unmount();
    if (fclose(ff.f) != 0) ok = false;
    if (!ok) remove(out);
    return ok ? 0 : 1;
}
