// Native tests of the media store core (src/media/media_core.c) on a NOR
// flash in RAM with power-cut injection (ram_flash.h).
#include <unity.h>

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ram_flash.h"

// Flashes live until tearDown(): a failed assertion longjmps out of the
// test, and media_unmount() still calls into the mounted flash.
static std::vector<std::unique_ptr<RamFlash>> g_flashes;
static RamFlash &new_flash(uint32_t pages) {
    g_flashes.emplace_back(new RamFlash(pages));
    return *g_flashes.back();
}

// The store's allocator (media_core_impl.c): counts what is allocated and
// fails on request.
static std::map<void *, size_t> g_allocs;
static bool g_alloc_fail = false;   // every allocation fails
static long g_alloc_fail_in = -1;   // >= 0: that many succeed, the next one fails (once)
static unsigned g_alloc_failed = 0; // allocations that were made to fail
extern "C" void *media_test_alloc(size_t n) {
    if (g_alloc_fail || g_alloc_fail_in == 0) {
        g_alloc_fail_in = -1;
        g_alloc_failed++;
        return nullptr;
    }
    if (g_alloc_fail_in > 0) g_alloc_fail_in--;
    void *p = malloc(n);
    g_allocs[p] = n;
    return p;
}
extern "C" void media_test_free(void *p) {
    g_allocs.erase(p);
    free(p);
}
static size_t alloc_bytes() {
    size_t n = 0;
    for (const auto &kv : g_allocs) n += kv.second;
    return n;
}
// The heap the index uses: everything but the free map (6 bytes per page).
static size_t index_bytes(const RamFlash &f) { return alloc_bytes() - 6 * (f.mem.size() / MEDIA_PAGE_SIZE); }

void setUp() {}
void tearDown() {
    g_alloc_fail = false;
    g_alloc_fail_in = -1;
    media_unmount();
    g_flashes.clear();
    TEST_ASSERT_EQUAL_UINT(0, g_allocs.size()); // nothing leaks
}

static const uint32_t PAGE = MEDIA_PAGE_SIZE;
static const uint32_t SECTOR = MEDIA_SECTOR_SIZE;

// ------------------------------------------------------------- helpers

static std::vector<uint8_t> pattern(uint32_t seed, uint32_t len) {
    std::vector<uint8_t> v(len);
    uint32_t x = seed * 2654435761u + 1u;
    for (uint32_t i = 0; i < len; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        v[i] = (uint8_t)x;
    }
    return v;
}

static std::array<uint8_t, 16> md5_of(uint32_t seed) {
    std::array<uint8_t, 16> m{};
    for (int i = 0; i < 16; i++) m[i] = (uint8_t)(seed * 31u + (uint32_t)i);
    return m;
}

static media_err_t put(media_bucket_t b, const std::string &key, const std::vector<uint8_t> &data,
                       uint32_t mtime = 1, uint32_t chunk = 1000) {
    media_writer_t *w = nullptr;
    media_err_t e = media_put_begin(b, key.c_str(), (uint32_t)data.size(), mtime, &w);
    if (e != MEDIA_OK) return e;
    for (size_t off = 0; off < data.size(); off += chunk) {
        const uint32_t n = (uint32_t)std::min<size_t>(chunk, data.size() - off);
        e = media_put_write(w, data.data() + off, n);
        if (e != MEDIA_OK) {
            media_put_abort(w);
            return e;
        }
    }
    const auto md5 = md5_of(mtime);
    return media_put_commit(w, md5.data());
}

static std::vector<uint8_t> read_file(media_bucket_t b, const std::string &key, media_err_t *err = nullptr) {
    media_file_t f{};
    const media_err_t e = media_open(b, key.c_str(), &f);
    if (err) *err = e;
    std::vector<uint8_t> v;
    if (e == MEDIA_OK) {
        if (f.size) v.assign(f.data, f.data + f.size);
        media_close(&f);
    }
    return v;
}

static media_stat_t scratch_stat;

static uint32_t alloc_of(uint32_t size) {
    if (size == 0) return 0;
    if (size <= MEDIA_SMALL_MAX) return (size + SECTOR - 1) / SECTOR * SECTOR;
    return (size + PAGE - 1) / PAGE * PAGE;
}

struct Entry {
    std::vector<uint8_t> data;
    uint32_t mtime = 0;
    bool operator==(const Entry &o) const { return data == o.data && mtime == o.mtime; }
};
using State = std::map<std::pair<int, std::string>, Entry>;

static State capture() {
    State s;
    for (int b = 0; b < MEDIA_BUCKET_COUNT; b++) {
        std::string after;
        media_stat_t st;
        while (media_next((media_bucket_t)b, after.c_str(), &st) == MEDIA_OK) {
            Entry e;
            media_err_t err;
            e.data = read_file((media_bucket_t)b, st.key, &err);
            TEST_ASSERT_EQUAL(MEDIA_OK, err);
            TEST_ASSERT_EQUAL_UINT32(st.size, e.data.size());
            const auto md5 = md5_of(st.mtime);
            TEST_ASSERT_EQUAL_MEMORY(md5.data(), st.md5, 16);
            e.mtime = st.mtime;
            s[{b, st.key}] = e;
            after = st.key;
        }
    }
    return s;
}

// No leak: what is not free belongs to a live entry (nothing open).
static void check_no_leak(const State &s) {
    media_usage_t u;
    media_usage(&u);
    uint32_t alloc = 0, derived = 0;
    for (const auto &kv : s) {
        alloc += alloc_of((uint32_t)kv.second.data.size());
        if (kv.first.first == MEDIA_BUCKET_RENDER) derived += alloc_of((uint32_t)kv.second.data.size());
    }
    TEST_ASSERT_EQUAL_UINT32(0, u.reclaimable);
    TEST_ASSERT_EQUAL_UINT32(alloc, u.used);
    TEST_ASSERT_EQUAL_UINT32(derived, u.derived);
    TEST_ASSERT_EQUAL_UINT32(u.total - alloc, u.free);
}

static void reboot(RamFlash &f) {
    media_unmount();
    f.revive();
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
}

static int maintain_all(int limit = 10000) {
    int n = 0;
    while (media_maintain() && n < limit) n++;
    return n;
}

static uint32_t crc32_ref(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// ---------------------------------------------------------------- basics

void test_format_and_header() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash)); // idempotent
    const uint8_t *h = f.mem.data();
    TEST_ASSERT_EQUAL_MEMORY("MDIR", h, 4);
    uint32_t crc = h[28] | h[29] << 8 | h[30] << 16 | (uint32_t)h[31] << 24;
    TEST_ASSERT_EQUAL_HEX32(crc32_ref(h, 28), crc);
    media_usage_t u;
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(6 * PAGE, u.total);
    TEST_ASSERT_EQUAL_UINT32(u.total, u.free);
    TEST_ASSERT_EQUAL_UINT32(0, u.used);

    // a store full of garbage is formatted
    media_unmount();
    RamFlash &g = new_flash(8);
    for (size_t i = 0; i < g.mem.size(); i++) g.mem[i] = (uint8_t)(i * 7 + 3);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&g.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "a.ttf", pattern(1, 5000)));
    TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_FONTS, "a.ttf") == pattern(1, 5000));
    TEST_ASSERT_EQUAL_UINT(0, g.violations);
    media_unmount();
    // too small / not page aligned
    RamFlash &t = new_flash(2);
    TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, media_mount(&t.flash));
    media_file_t file;
    TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, media_open(MEDIA_BUCKET_FONTS, "a", &file));
    TEST_ASSERT_FALSE(media_maintain());
}

void test_put_open_close_delete() {
    RamFlash &f = new_flash(16);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    const auto small = pattern(1, 3000), large = pattern(2, 200000);
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "small.ttf", small, 10));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "large.ttf", large, 20));
    media_stat_t st;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_FONTS, "large.ttf", &st));
    TEST_ASSERT_EQUAL_UINT32(200000, st.size);
    TEST_ASSERT_EQUAL_UINT32(20, st.mtime);
    TEST_ASSERT_EQUAL_STRING("large.ttf", st.key);
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_stat(MEDIA_BUCKET_IMAGES, "large.ttf", &st));

    media_file_t a{}, b{};
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "small.ttf", &a));
    TEST_ASSERT_EQUAL_UINT32(3000, a.size);
    TEST_ASSERT_EQUAL_MEMORY(small.data(), a.data, 3000);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "large.ttf", &b));
    TEST_ASSERT_EQUAL_MEMORY(large.data(), b.data, large.size());
    TEST_ASSERT_NOT_EQUAL(0, a.handle);
    TEST_ASSERT_NOT_EQUAL(a.handle, b.handle);
    media_close(&a);
    TEST_ASSERT_EQUAL_UINT32(0, a.handle);
    media_close(&a); // zeroed: harmless
    media_close(&b);
    TEST_ASSERT_EQUAL_UINT(0, f.maps_live);

    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "small.ttf"));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_delete(MEDIA_BUCKET_FONTS, "small.ttf"));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_open(MEDIA_BUCKET_FONTS, "small.ttf", &a));
    TEST_ASSERT_EQUAL_PTR(nullptr, a.data);
    State s = capture();
    TEST_ASSERT_EQUAL(1, (int)s.size());
    check_no_leak(s);
    reboot(f);
    TEST_ASSERT_TRUE(capture() == s);
    check_no_leak(s);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

void test_same_key_shares_mapping() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "a", pattern(1, 100)));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "b", pattern(2, 100)));
    media_file_t x{}, y{}, z{};
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "a", &x));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "a", &y));
    TEST_ASSERT_EQUAL_UINT32(x.handle, y.handle);
    TEST_ASSERT_EQUAL_PTR(x.data, y.data);
    TEST_ASSERT_EQUAL_UINT(1, f.maps_live);
    // b is in the same shared page: the page mapping is shared too
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "b", &z));
    TEST_ASSERT_EQUAL_UINT(1, f.maps_live);
    TEST_ASSERT_EQUAL_PTR(x.data + SECTOR, z.data);
    media_file_t stale = x;
    media_close(&x);
    TEST_ASSERT_EQUAL_UINT(1, f.maps_live);
    TEST_ASSERT_EQUAL_MEMORY(pattern(1, 100).data(), y.data, 100);
    media_close(&y);
    media_close(&stale); // already fully released: ignored
    media_close(&z);
    TEST_ASSERT_EQUAL_UINT(0, f.maps_live);
}

void test_zero_size_and_writer_rules() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "empty", {}));
    media_file_t e{};
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_IMAGES, "empty", &e));
    TEST_ASSERT_EQUAL_UINT32(0, e.size);
    TEST_ASSERT_EQUAL_PTR(nullptr, e.data);
    TEST_ASSERT_NOT_EQUAL(0, e.handle);
    media_close(&e);

    media_usage_t u0, u1;
    media_usage(&u0);
    media_writer_t *w = nullptr;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "x", 10, 1, &w));
    const auto d = pattern(3, 20);
    TEST_ASSERT_EQUAL(MEDIA_ERR_SIZE, media_put_write(w, d.data(), 11)); // more than reserved
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_write(w, d.data(), 4));         // writer still valid
    TEST_ASSERT_EQUAL(MEDIA_ERR_SIZE, media_put_commit(w, nullptr));      // too few: consumed
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_commit(w, nullptr));   // gone
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_write(w, d.data(), 1));
    media_put_abort(w); // ignored
    media_put_abort(nullptr);
    media_usage(&u1);
    TEST_ASSERT_EQUAL_UINT32(u0.free, u1.free);
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_stat(MEDIA_BUCKET_FONTS, "x", &scratch_stat));

    // abort releases the reservation
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "x", 100000, 1, &w));
    media_usage(&u1);
    TEST_ASSERT_EQUAL_UINT32(u0.free - 2 * PAGE, u1.free);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_write(w, d.data(), 20));
    media_put_abort(w);
    media_usage(&u1);
    TEST_ASSERT_EQUAL_UINT32(u0.free, u1.free);

    // at most two uploads at once
    media_writer_t *w1, *w2, *w3;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "1", 1, 1, &w1));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "2", 1, 1, &w2));
    TEST_ASSERT_EQUAL(MEDIA_ERR_BUSY, media_put_begin(MEDIA_BUCKET_FONTS, "3", 1, 1, &w3));
    TEST_ASSERT_EQUAL_PTR(nullptr, w3);
    media_put_abort(w1);
    media_put_abort(w2);
    check_no_leak(capture());
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

void test_bad_keys() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    media_writer_t *w;
    media_file_t file;
    media_stat_t st;
    const std::string k96(MEDIA_KEY_MAX, 'k'), k97(MEDIA_KEY_MAX + 1, 'k');
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_begin(MEDIA_BUCKET_FONTS, nullptr, 1, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_begin(MEDIA_BUCKET_FONTS, "", 1, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_begin(MEDIA_BUCKET_FONTS, k97.c_str(), 1, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_begin(MEDIA_BUCKET_FONTS, "a\nb", 1, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_begin(MEDIA_BUCKET_FONTS, "a\x7f", 1, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_begin((media_bucket_t)3, "a", 1, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_begin((media_bucket_t)-1, "a", 1, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_begin(MEDIA_BUCKET_FONTS, "a", 1, 1, nullptr));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_open(MEDIA_BUCKET_FONTS, k97.c_str(), &file));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_open(MEDIA_BUCKET_FONTS, "a", nullptr));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_delete(MEDIA_BUCKET_FONTS, ""));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_stat(MEDIA_BUCKET_FONTS, "a", nullptr));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_next((media_bucket_t)7, nullptr, &st));
    TEST_ASSERT_EQUAL(MEDIA_ERR_INVALID, media_put_begin(MEDIA_BUCKET_FONTS, "a", MEDIA_FILE_MAX + 1, 1, &w));
    // the longest key, '/', UTF-8
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, k96, pattern(1, 10)));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "dir/sub/caf\xc3\xa9.bin", pattern(2, 10)));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_FONTS, k96.c_str(), &st));
    TEST_ASSERT_EQUAL_STRING(k96.c_str(), st.key);
    reboot(f);
    TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_IMAGES, "dir/sub/caf\xc3\xa9.bin") == pattern(2, 10));
}

void test_replace_and_delete_while_open() {
    RamFlash &f = new_flash(16);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    const auto v1 = pattern(1, 150000), v2 = pattern(2, 20000);
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "f", v1, 1));
    media_file_t old{};
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "f", &old));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "f", v2, 2));
    media_usage_t u;
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(3 * PAGE, u.reclaimable);
    TEST_ASSERT_EQUAL_UINT32(5 * SECTOR, u.used);
    TEST_ASSERT_EQUAL_UINT32(u.total - u.used - u.reclaimable, u.free);
    media_file_t nu{};
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "f", &nu));
    TEST_ASSERT_TRUE(nu.handle != old.handle);
    TEST_ASSERT_EQUAL_MEMORY(v2.data(), nu.data, v2.size());
    // fill the store: the old pages must not be reused while open
    int n = 0;
    while (put(MEDIA_BUCKET_IMAGES, "fill" + std::to_string(n), pattern(100 + n, 40000)) == MEDIA_OK) n++;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_MEMORY(v1.data(), old.data, v1.size());
    TEST_ASSERT_EQUAL_UINT(0, f.map_violations);
    media_close(&old);
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(0, u.reclaimable);
    TEST_ASSERT_EQUAL_UINT32(3 * PAGE + 11 * SECTOR, u.free); // freed on the last close
    // delete while open
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "f"));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_stat(MEDIA_BUCKET_FONTS, "f", &scratch_stat));
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(5 * SECTOR, u.reclaimable);
    TEST_ASSERT_EQUAL_MEMORY(v2.data(), nu.data, v2.size());
    media_close(&nu);
    check_no_leak(capture());
    // reboot while a deleted file is open: nothing leaks either
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_IMAGES, "fill0", &old));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "fill0"));
    reboot(f);
    check_no_leak(capture());
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

void test_usage_numbers() {
    RamFlash &f = new_flash(12);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "a", pattern(1, 1)));         // 1 sector
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "b", pattern(2, 32768)));     // 8 sectors
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "c", pattern(3, 32769)));     // 1 page
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "r", pattern(4, 70000)));    // 2 pages
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "s", pattern(5, 5000)));     // 2 sectors
    media_usage_t u;
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(10 * PAGE, u.total);
    TEST_ASSERT_EQUAL_UINT32(11 * SECTOR + 3 * PAGE, u.used);
    TEST_ASSERT_EQUAL_UINT32(2 * PAGE + 2 * SECTOR, u.derived);
    TEST_ASSERT_EQUAL_UINT32(0, u.reclaimable);
    TEST_ASSERT_EQUAL_UINT32(u.total - u.used, u.free);
    // small files packed: a, b, s share one page
    TEST_ASSERT_EQUAL_UINT32(10 * PAGE - 4 * PAGE, u.free - (16 - 11) * SECTOR);
    // a reservation counts as used
    media_writer_t *w;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "d", 4096, 1, &w));
    media_usage_t u2;
    media_usage(&u2);
    TEST_ASSERT_EQUAL_UINT32(u.used + SECTOR, u2.used);
    media_put_abort(w);
    check_no_leak(capture());
}

void test_full_store_no_space() {
    RamFlash &f = new_flash(8); // 6 data pages
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "a", pattern(1, 4 * PAGE)));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "b", pattern(2, 20000)));
    const State before = capture();
    const std::vector<uint8_t> mem = f.mem;
    const unsigned writes = f.writes, erases = f.erases;
    media_writer_t *w = (media_writer_t *)1;
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, media_put_begin(MEDIA_BUCKET_FONTS, "c", 2 * PAGE, 1, &w));
    TEST_ASSERT_EQUAL_PTR(nullptr, w);
    // replacing needs room for both versions
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, media_put_begin(MEDIA_BUCKET_FONTS, "a", 2 * PAGE, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, media_put_begin(MEDIA_BUCKET_RENDER, "r", 5 * PAGE, 1, &w));
    TEST_ASSERT_EQUAL_UINT(writes, f.writes);
    TEST_ASSERT_EQUAL_UINT(erases, f.erases);
    TEST_ASSERT_TRUE(mem == f.mem);
    TEST_ASSERT_TRUE(before == capture());
    // what still fits does
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "c", pattern(3, PAGE)));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, put(MEDIA_BUCKET_FONTS, "d", pattern(3, 8 * SECTOR + 1))); // large
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "d", pattern(3, 8 * SECTOR)));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, put(MEDIA_BUCKET_FONTS, "e", pattern(3, 4 * SECTOR)));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "e", pattern(3, 3 * SECTOR)));
    media_usage_t u;
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(0, u.free);
    check_no_leak(capture());
}

void test_render_eviction_order() {
    RamFlash &f = new_flash(11); // 9 data pages
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "font", pattern(1, 2 * PAGE)));
    for (int i = 0; i < 5; i++)
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "r" + std::to_string(i), pattern(10 + i, PAGE)));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "small", pattern(20, 100)));
    // 1 page and 15 sectors free; r0 is open, so r1 and r2 go first
    media_file_t r0{};
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_RENDER, "r0", &r0));
    // nothing can make 9 pages: no eviction at all
    media_writer_t *w;
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, media_put_begin(MEDIA_BUCKET_IMAGES, "huge", 9 * PAGE, 1, &w));
    TEST_ASSERT_EQUAL(7, (int)capture().size());
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "img", pattern(30, 3 * PAGE)));
    State s = capture();
    TEST_ASSERT_TRUE(s.count({MEDIA_BUCKET_RENDER, "r0"}));
    TEST_ASSERT_FALSE(s.count({MEDIA_BUCKET_RENDER, "r1"}));
    TEST_ASSERT_FALSE(s.count({MEDIA_BUCKET_RENDER, "r2"}));
    TEST_ASSERT_TRUE(s.count({MEDIA_BUCKET_RENDER, "r3"}));
    TEST_ASSERT_TRUE(s.count({MEDIA_BUCKET_RENDER, "r4"}));
    TEST_ASSERT_TRUE(s.count({MEDIA_BUCKET_RENDER, "small"}));
    TEST_ASSERT_EQUAL_MEMORY(pattern(10, PAGE).data(), r0.data, PAGE);
    media_close(&r0);
    // user files are never evicted; RENDER can evict RENDER
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "r5", pattern(15, 2 * PAGE)));
    s = capture();
    TEST_ASSERT_FALSE(s.count({MEDIA_BUCKET_RENDER, "r0"}));
    TEST_ASSERT_TRUE(s.count({MEDIA_BUCKET_IMAGES, "img"}));
    TEST_ASSERT_TRUE(s.count({MEDIA_BUCKET_FONTS, "font"}));
    check_no_leak(s);
    reboot(f);
    TEST_ASSERT_TRUE(capture() == s);
    check_no_leak(s);
}

void test_concurrent_reservations() {
    RamFlash &f = new_flash(8); // 6 data pages
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    media_writer_t *a, *b;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "a", 4 * PAGE, 1, &a));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, media_put_begin(MEDIA_BUCKET_FONTS, "b", 3 * PAGE, 1, &b));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "b", 2 * PAGE, 1, &b));
    // interleaved writes
    const auto da = pattern(1, 4 * PAGE), db = pattern(2, 2 * PAGE);
    for (uint32_t off = 0; off < 4 * PAGE; off += 1460) {
        const uint32_t n = std::min<uint32_t>(1460, 4 * PAGE - off);
        TEST_ASSERT_EQUAL(MEDIA_OK, media_put_write(a, da.data() + off, n));
        if (off < 2 * PAGE)
            TEST_ASSERT_EQUAL(MEDIA_OK, media_put_write(b, db.data() + off, std::min<uint32_t>(n, 2 * PAGE - off)));
    }
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_commit(b, md5_of(1).data()));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_commit(a, md5_of(1).data()));
    TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_FONTS, "a") == da);
    TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_FONTS, "b") == db);
    // two uploads of the same key: the last commit wins, nothing leaks
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "a"));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "k", 100, 1, &a));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "k", 200, 2, &b));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_write(a, pattern(5, 100).data(), 100));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_write(b, pattern(6, 200).data(), 200));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_commit(b, md5_of(2).data()));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_commit(a, md5_of(1).data()));
    TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_FONTS, "k") == pattern(5, 100));
    check_no_leak(capture());
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

void test_open_table_limit() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    media_file_t files[MEDIA_MAX_OPEN + 1];
    for (int i = 0; i <= MEDIA_MAX_OPEN; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, std::to_string(i), pattern(i, 10)));
    for (int i = 0; i < MEDIA_MAX_OPEN; i++)
        TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, std::to_string(i).c_str(), &files[i]));
    TEST_ASSERT_EQUAL(MEDIA_ERR_BUSY, media_open(MEDIA_BUCKET_FONTS, std::to_string(MEDIA_MAX_OPEN).c_str(), &files[MEDIA_MAX_OPEN]));
    // an open file can still be opened again
    media_file_t again;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "3", &again));
    media_close(&again);
    media_close(&files[0]);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, std::to_string(MEDIA_MAX_OPEN).c_str(), &files[MEDIA_MAX_OPEN]));
    for (int i = 1; i <= MEDIA_MAX_OPEN; i++) media_close(&files[i]);
    TEST_ASSERT_EQUAL_UINT(0, f.maps_live);
}

void test_next_order_and_bounds() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    std::vector<std::string> keys = {"b", "a", "a/b", "ab", "A", "a/", "\xc3\xa9", "z~", "0", std::string(96, 'z'), "aa"};
    for (size_t i = 0; i < keys.size(); i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, keys[i], {}));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "other", {}));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "ab", pattern(1, 3))); // replaced: listed once
    std::vector<std::string> sorted = keys;
    std::sort(sorted.begin(), sorted.end()); // std::string compares bytes as unsigned char
    std::vector<std::string> got;
    media_stat_t st;
    const char *after = nullptr;
    std::string last;
    while (media_next(MEDIA_BUCKET_FONTS, after, &st) == MEDIA_OK) {
        got.push_back(st.key);
        last = st.key;
        after = last.c_str();
    }
    TEST_ASSERT_EQUAL((int)sorted.size(), (int)got.size());
    for (size_t i = 0; i < sorted.size(); i++) TEST_ASSERT_EQUAL_STRING(sorted[i].c_str(), got[i].c_str());
    // `after` aliasing out->key
    size_t aliased = 0;
    for (media_err_t e = media_next(MEDIA_BUCKET_FONTS, nullptr, &st); e == MEDIA_OK;
         e = media_next(MEDIA_BUCKET_FONTS, st.key, &st))
        TEST_ASSERT_EQUAL_STRING(sorted[aliased++].c_str(), st.key);
    TEST_ASSERT_EQUAL(sorted.size(), aliased);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_next(MEDIA_BUCKET_FONTS, "", &st));
    TEST_ASSERT_EQUAL_STRING("0", st.key);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_next(MEDIA_BUCKET_FONTS, "a/a", &st)); // not a key itself
    TEST_ASSERT_EQUAL_STRING("a/b", st.key);
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_next(MEDIA_BUCKET_FONTS, sorted.back().c_str(), &st));
    const std::string longer(200, '\xff');
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_next(MEDIA_BUCKET_FONTS, longer.c_str(), &st));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_next(MEDIA_BUCKET_RENDER, nullptr, &st));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_next(MEDIA_BUCKET_IMAGES, nullptr, &st));
    TEST_ASSERT_EQUAL_STRING("other", st.key);
}

void test_max_sizes() {
    RamFlash &f = new_flash(0x500000 / PAGE); // the real partition: 78 pages
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    const auto big = pattern(7, MEDIA_FILE_MAX);
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "big", big, 1, 4096));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, put(MEDIA_BUCKET_FONTS, "big2", pattern(8, 2u * 1024 * 1024)));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "big"));
    const auto two = pattern(8, 2u * 1024 * 1024);
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "two", two, 1, 1460));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "two2", two, 1, 1460));
    TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_FONTS, "two2") == two);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "two2"));
    // at least 300 live entries with the longest keys
    int n = 0;
    for (; n < 320; n++) {
        std::string key = std::string(MEDIA_KEY_MAX - 4, 'k') + std::to_string(1000 + n);
        if (put(MEDIA_BUCKET_IMAGES, key, pattern(n, 1 + n % 50)) != MEDIA_OK) break;
    }
    TEST_ASSERT_TRUE(n >= 300);
    const State s = capture();
    TEST_ASSERT_EQUAL(n + 1, (int)s.size());
    reboot(f);
    TEST_ASSERT_TRUE(capture() == s);
    check_no_leak(s);
    // and they survive a snapshot (forced by many replaces)
    for (int i = 0; i < 300; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "x", {}, (uint32_t)i));
    State s2 = s;
    s2[{MEDIA_BUCKET_RENDER, "x"}] = Entry{{}, 299};
    TEST_ASSERT_TRUE(capture() == s2);
    reboot(f);
    TEST_ASSERT_TRUE(capture() == s2);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
    TEST_ASSERT_EQUAL_UINT(0, f.map_violations);
}

void test_directory_limit() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    int n = 0;
    media_err_t e;
    while ((e = put(MEDIA_BUCKET_IMAGES, std::string(90, 'e') + std::to_string(10000 + n), {})) == MEDIA_OK) n++;
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, e);
    TEST_ASSERT_TRUE(n >= 300);
    // delete and replace still work when the directory is at its limit
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, (std::string(90, 'e') + "10000").c_str()));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, std::string(90, 'e') + "10001", pattern(1, 1)));
    for (int i = 0; i < 200; i++) TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, (std::string(90, 'e') + std::to_string(10002 + i)).c_str()));
    const State s = capture();
    TEST_ASSERT_EQUAL(n - 201, (int)s.size());
    reboot(f);
    TEST_ASSERT_TRUE(capture() == s);
}

// Random operations against a model, with reboots: many directory
// rollovers (by size and by record count).
void test_rollover_many_records() {
    RamFlash &f = new_flash(20);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    State model;
    std::mt19937 rng(7);
    const unsigned headers0 = f.header_writes;
    for (int i = 0; i < 6000; i++) {
        const int b = (int)(rng() % 3);
        const std::string key = "k" + std::to_string(rng() % 40);
        const int op = (int)(rng() % 10);
        if (op < 6) {
            const uint32_t sizes[] = {0, 1, 100, 4096, 5000, 32768, 32769, 70000};
            const uint32_t size = sizes[rng() % 8];
            const auto data = pattern((uint32_t)i, size);
            const media_err_t e = put((media_bucket_t)b, key, data, (uint32_t)i, 1 + rng() % 3000);
            if (e == MEDIA_OK) model[{b, key}] = Entry{data, (uint32_t)i};
            else TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, e);
            // RENDER entries may have been evicted to make room
            for (auto it = model.begin(); it != model.end();) {
                media_stat_t st;
                if (it->first.first == MEDIA_BUCKET_RENDER &&
                    media_stat(MEDIA_BUCKET_RENDER, it->first.second.c_str(), &st) == MEDIA_ERR_NOT_FOUND)
                    it = model.erase(it);
                else ++it;
            }
        } else if (op < 9) {
            const media_err_t e = media_delete((media_bucket_t)b, key.c_str());
            TEST_ASSERT_EQUAL(model.count({b, key}) ? MEDIA_OK : MEDIA_ERR_NOT_FOUND, e);
            model.erase({b, key});
        } else {
            for (int k = 0; k < 5; k++) media_maintain();
        }
        if (i % 500 == 499) {
            TEST_ASSERT_TRUE(capture() == model);
            check_no_leak(model);
            reboot(f);
            TEST_ASSERT_TRUE(capture() == model);
            check_no_leak(model);
        }
    }
    TEST_ASSERT_TRUE(f.header_writes - headers0 >= 4); // snapshots happened (by size and by count)
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
    TEST_ASSERT_EQUAL_UINT(0, f.map_violations);
}

void test_small_packing_fragmentation_compaction() {
    RamFlash &f = new_flash(8); // 6 data pages
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    // 16 x 4 KB fill exactly one page
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "p" + std::to_string(i), pattern(i, 4096)));
    media_usage_t u;
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(5 * PAGE, u.free);
    // four more pages of small files, then delete most of them: sparse pages
    for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "q" + std::to_string(i), pattern(100 + i, 3000)));
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(PAGE, u.free);
    for (int i = 0; i < 64; i++)
        if (i % 16 > 2) TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, ("q" + std::to_string(i)).c_str()));
    for (int i = 0; i < 16; i++)
        if (i % 4) TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, ("p" + std::to_string(i)).c_str()));
    // 16 sectors used over 5 pages: plenty of sectors, but no 3 free pages
    media_writer_t *w;
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, media_put_begin(MEDIA_BUCKET_FONTS, "large", 3 * PAGE, 1, &w));
    // a file that is open is not moved
    media_file_t open_q0{};
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_IMAGES, "q0", &open_q0));
    const State before = capture();
    const unsigned e0 = f.erases;
    int calls = 0;
    while (media_maintain()) {
        TEST_ASSERT_TRUE(f.erases - e0 <= (unsigned)calls + 1); // at most one erase per call
        calls++;
        TEST_ASSERT_TRUE(calls < 1000);
    }
    TEST_ASSERT_TRUE(capture() == before);
    TEST_ASSERT_EQUAL_MEMORY(pattern(100, 3000).data(), open_q0.data, 3000);
    // compaction freed whole pages (one page keeps q0, which is open)
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "large", pattern(9, 3 * PAGE)));
    media_close(&open_q0);
    State after = capture();
    check_no_leak(after);
    reboot(f);
    TEST_ASSERT_TRUE(capture() == after);
    check_no_leak(after);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

void test_preerase_and_sync_erase() {
    RamFlash &f = new_flash(10);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, std::to_string(i), pattern(i, 50000)));
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, std::to_string(i).c_str()));
    // after a reboot the erased state is unknown: an upload erases what it needs
    reboot(f);
    const unsigned e0 = f.erases;
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "a", pattern(1, 3 * PAGE)));
    TEST_ASSERT_EQUAL_UINT(e0 + 3, f.erases);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
    // maintain verifies blank pages without erasing them, erases the others
    reboot(f);
    unsigned e1 = f.erases;
    while (media_maintain()) {
        TEST_ASSERT_TRUE(f.erases - e1 <= 1);
        e1 = f.erases;
    }
    // uploads now find erased space
    const unsigned e2 = f.erases;
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "b", pattern(2, 3 * PAGE)));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "c", pattern(3, 5000)));
    TEST_ASSERT_EQUAL_UINT(e2, f.erases);
    TEST_ASSERT_FALSE(media_maintain());
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

// ------------------------------------------------------------ power cuts

// Runs `op` on a copy of the store `setup` built, cutting the power after
// 0, 1, 2, ... flash operations (or bytes), reboots, and checks the state
// is exactly the old or the new one, with no space leaked and the store
// still writable without programming non-erased flash.
// `s` is `ref` with possibly some RENDER entries gone (evicted).
static bool minus_render(const State &s, const State &ref) {
    for (const auto &kv : s) {
        auto it = ref.find(kv.first);
        if (it == ref.end() || !(it->second == kv.second)) return false;
    }
    for (const auto &kv : ref)
        if (kv.first.first != MEDIA_BUCKET_RENDER && !s.count(kv.first)) return false;
    return true;
}

static void sweep(uint32_t pages, const std::function<void()> &setup, const std::function<void()> &op,
                  bool premaintain, bool by_bytes = false, bool render_may_vanish = false) {
    RamFlash *f = &new_flash(pages);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f->flash));
    setup();
    media_unmount();
    const std::vector<uint8_t> base = f->mem;

    auto start = [&]() {
        media_unmount();
        f->mem = base;
        f->revive();
        TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f->flash));
        if (premaintain) maintain_all();
        f->violations = 0;
    };
    start();
    const State old_state = capture();
    const unsigned w0 = f->writes, e0 = f->erases;
    op();
    const State new_state = capture();
    const unsigned ops = (f->writes - w0) + (f->erases - e0);
    TEST_ASSERT_TRUE(ops > 0);
    long total_bytes = 0;
    if (by_bytes) {
        // measure the bytes programmed by the op
        start();
        long lo = 0;
        f->cut_after_bytes(1L << 40);
        op();
        lo = (1L << 40) - f->bytes_left;
        total_bytes = lo;
    }
    const long steps = by_bytes ? total_bytes + 1 : (long)ops + 1;
    const long stride = by_bytes ? std::max(1L, total_bytes / 400) : 1;
    int saw_old = 0, saw_new = 0;
    for (long cut = 0;; cut = cut + stride < steps ? cut + stride : steps) {
        start();
        if (by_bytes) f->cut_after_bytes(cut);
        else f->cut_after_ops(cut);
        op();
        const bool hit = f->hit;
        reboot(*f);
        const State s = capture();
        bool is_old = s == old_state, is_new = s == new_state;
        if (render_may_vanish && !is_old && !is_new) is_old = minus_render(s, old_state) || minus_render(s, new_state);
        if (!is_old && !is_new) {
            char msg[64];
            snprintf(msg, sizeof msg, "cut %ld of %ld: neither old nor new", cut, steps);
            TEST_FAIL_MESSAGE(msg);
        }
        saw_old += is_old;
        saw_new += is_new;
        check_no_leak(s);
        TEST_ASSERT_EQUAL_UINT(0, f->violations);
        // the store still works, before and after maintenance
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "post", pattern(99, 5000)));
        TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_IMAGES, "post") == pattern(99, 5000));
        TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "post"));
        maintain_all();
        TEST_ASSERT_TRUE(capture() == s);
        check_no_leak(s);
        TEST_ASSERT_EQUAL_UINT(0, f->violations);
        TEST_ASSERT_EQUAL_UINT(0, f->map_violations);
        if (!hit) {
            TEST_ASSERT_TRUE(is_new);
            break;
        }
        TEST_ASSERT_TRUE(cut < steps);
    }
    TEST_ASSERT_TRUE(saw_old > 0);
    TEST_ASSERT_TRUE(saw_new > 0);
    media_unmount();
}

static void base_files() {
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "medium.ttf", pattern(1, 90000), 1));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "icons.ttf", pattern(2, 9000), 2));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "r.bin", pattern(3, 20000), 3));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "icons.ttf"));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "icons.ttf", pattern(4, 9000), 4));
}

void test_powercut_put() {
    for (int pre = 0; pre < 2; pre++) {
        sweep(12, base_files, [] { put(MEDIA_BUCKET_FONTS, "new.ttf", pattern(10, 6000), 10, 1000); }, pre);
        sweep(12, base_files, [] { put(MEDIA_BUCKET_FONTS, "new.ttf", pattern(11, 140000), 11, 1460); }, pre);
        sweep(12, base_files, [] { put(MEDIA_BUCKET_IMAGES, "zero", {}, 12); }, pre);
    }
    sweep(12, base_files, [] { put(MEDIA_BUCKET_FONTS, "new.ttf", pattern(10, 300), 10, 100); }, false, true);
}

void test_powercut_replace() {
    for (int pre = 0; pre < 2; pre++) {
        sweep(12, base_files, [] { put(MEDIA_BUCKET_FONTS, "medium.ttf", pattern(20, 7000), 20); }, pre);   // large -> small
        sweep(12, base_files, [] { put(MEDIA_BUCKET_FONTS, "icons.ttf", pattern(21, 100000), 21); }, pre); // small -> large
        sweep(12, base_files, [] { put(MEDIA_BUCKET_FONTS, "medium.ttf", pattern(22, 80000), 22); }, pre); // large -> large
    }
    sweep(12, base_files, [] { put(MEDIA_BUCKET_FONTS, "icons.ttf", pattern(23, 200), 23); }, false, true);
}

void test_powercut_delete() {
    sweep(12, base_files, [] { media_delete(MEDIA_BUCKET_FONTS, "medium.ttf"); }, false);
    sweep(12, base_files, [] { media_delete(MEDIA_BUCKET_FONTS, "icons.ttf"); }, true, true);
}

void test_powercut_eviction() {
    // the put needs the RENDER file's space
    sweep(6, [] {
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "r1", pattern(1, 2 * PAGE), 1));
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "r2", pattern(2, PAGE), 2));
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "f", pattern(3, 100), 3));
    }, [] { put(MEDIA_BUCKET_FONTS, "g", pattern(4, 2 * PAGE + 1), 4); }, false, false, true);
}

// Number of replaces of "z" after `setup` until one of them writes a
// directory header (a snapshot).
static int replaces_until_snapshot(const std::function<void()> &setup) {
    RamFlash &f = new_flash(12);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    setup();
    media_unmount();
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash)); // as sweep() sees it
    for (int i = 0; i < 5000; i++) {
        const unsigned h = f.header_writes;
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "z", pattern(i, 10), 1000 + (uint32_t)i));
        if (f.header_writes != h) {
            media_unmount();
            return i;
        }
    }
    TEST_FAIL_MESSAGE("no snapshot");
    return -1;
}

void test_powercut_snapshot_rollover() {
    auto setup0 = [] {
        base_files();
        for (int i = 0; i < 20; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "i" + std::to_string(i), pattern(50 + i, 100 + i), 5));
    };
    const int k = replaces_until_snapshot(setup0);
    auto setup = [&] {
        setup0();
        for (int i = 0; i < k; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "z", pattern(i, 10), 1000 + (uint32_t)i));
    };
    for (int pre = 0; pre < 2; pre++) {
        // the replace that rolls the directory over
        sweep(12, setup, [&] { put(MEDIA_BUCKET_RENDER, "z", pattern(k, 10), 1000 + (uint32_t)k); }, pre);
        // a delete that rolls over
        sweep(12, setup, [] { media_delete(MEDIA_BUCKET_IMAGES, "i3"); }, pre);
    }
    sweep(12, setup, [&] { put(MEDIA_BUCKET_RENDER, "z", pattern(k, 10), 1000 + (uint32_t)k); }, true, true);
}

void test_powercut_compaction() {
    auto setup = [] {
        for (int i = 0; i < 32; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "q" + std::to_string(i), pattern(i, 6000), 1));
        for (int i = 0; i < 32; i++)
            if (i % 8 > 1) TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, ("q" + std::to_string(i)).c_str()));
    };
    // every maintain call until compaction is done: the state never changes
    sweep(8, setup, [] { maintain_all(); }, false);
}

// ------------------------------------------------------------------ wear
//
// A device-sized store (78 pages, 5 MB) holding 1.5 MB of fonts that never
// change, and a workload that rewrites files over and over. Wear is counted
// per 4 KB sector by the flash; the ideal is every erase spread evenly over
// the sectors the static files do not hold.

static const uint32_t kWearPages = 78;
static const unsigned kWearStaticSectors = 24 * (PAGE / SECTOR);

struct WearStats {
    unsigned cycles = 0, total = 0, max = 0;
    double ideal = 0, ratio = 0;
};

static WearStats wear_stats(const RamFlash &f, unsigned cycles) {
    const size_t first = MEDIA_DIR_PAGES * (PAGE / SECTOR);
    WearStats w;
    w.cycles = cycles;
    for (size_t s = first; s < f.sector_erases.size(); s++) {
        w.total += f.sector_erases[s];
        w.max = std::max(w.max, f.sector_erases[s]);
    }
    const size_t pool = f.sector_erases.size() - first - kWearStaticSectors;
    w.ideal = (double)w.total / (double)pool;
    w.ratio = w.ideal > 0 ? (double)w.max / w.ideal : 0;
    return w;
}

static void wear_report(const char *name, const WearStats &w) {
    printf("wear %-8s %5u cycles: %7u sector erases, max %5u per sector, ideal %7.1f, max/ideal %5.2f\n", name,
           w.cycles, w.total, w.max, w.ideal, w.ratio);
}

static const std::vector<uint8_t> &wear_blob(int which) {
    static const std::vector<uint8_t> frames = pattern(7, 690 * 1024); // decoded frameset, 11 pages
    static const std::vector<uint8_t> raw = pattern(8, 95 * 1024);     // download, 2 pages
    static const std::vector<uint8_t> jpeg = pattern(9, 14 * 1024);    // history frame, 4 sectors
    return which == 0 ? frames : which == 1 ? raw : jpeg;
}

// Download, decode into a new frameset (replaces the old one), drop the download.
static void wear_cycle_frames(int i) {
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "raw/radar", wear_blob(1), 1000 + (uint32_t)i, 4096));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "fs/radar", wear_blob(0), 1000 + (uint32_t)i, 4096));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_RENDER, "raw/radar"));
}

// A rolling history of six small frames: add the newest, drop the oldest.
static void wear_cycle_history(int i) {
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "h/" + std::to_string(i), wear_blob(2), 1000 + (uint32_t)i, 4096));
    if (i >= 6) TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_RENDER, ("h/" + std::to_string(i - 6)).c_str()));
}

static void wear_cycle_mixed(int i) {
    wear_cycle_history(i);
    if (i % 4 == 0) wear_cycle_frames(i / 4);
}

static WearStats run_wear(const char *name, void (*cycle)(int), int cycles) {
    RamFlash &f = new_flash(kWearPages);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "icons.ttf", pattern(1, 17 * PAGE - 1000), 1, 65536));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "text.ttf", pattern(2, 7 * PAGE - 500), 1, 65536));
    maintain_all();
    std::fill(f.sector_erases.begin(), f.sector_erases.end(), 0u);
    for (int i = 0; i < cycles; i++) {
        cycle(i);
        maintain_all(); // the device runs media_maintain() from loop()
        if (i % 1000 == 999) reboot(f);
    }
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
    TEST_ASSERT_EQUAL_UINT(0, f.map_violations);
    check_no_leak(capture());
    const WearStats w = wear_stats(f, (unsigned)cycles);
    wear_report(name, w);
    return w;
}

// Before next-fit allocation (best-fit, lowest page first) these were 4.0,
// 29.7 and 3.7 times the ideal: the same pages took every rewrite.
static const double kWearMaxRatio = 1.5;

void test_wear_frames() { TEST_ASSERT_TRUE(run_wear("frames", wear_cycle_frames, 2000).ratio <= kWearMaxRatio); }
void test_wear_history() { TEST_ASSERT_TRUE(run_wear("history", wear_cycle_history, 6000).ratio <= kWearMaxRatio); }
void test_wear_mixed() { TEST_ASSERT_TRUE(run_wear("mixed", wear_cycle_mixed, 4000).ratio <= kWearMaxRatio); }

// The page a fresh 1-page file landed on (the flash holds its pattern).
static int landing_page(const RamFlash &f, const std::vector<uint8_t> &data) {
    for (uint32_t p = MEDIA_DIR_PAGES; p < f.mem.size() / PAGE; p++)
        if (!memcmp(f.mem.data() + p * PAGE, data.data(), 64)) return (int)p;
    return -1;
}

// The sweep starts somewhere else after a reboot, and allocation still works.
void test_cursor_seed_varies() {
    RamFlash &f = new_flash(40);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    std::vector<int> pages;
    for (int boot = 0; boot < 8; boot++) {
        const std::vector<uint8_t> data = pattern(500 + boot, PAGE);
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "probe", data, 1, 4096));
        pages.push_back(landing_page(f, data));
        TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_RENDER, "probe") == data);
        TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_RENDER, "probe"));
        maintain_all();
        reboot(f);
    }
    std::sort(pages.begin(), pages.end());
    TEST_ASSERT_TRUE(std::unique(pages.begin(), pages.end()) - pages.begin() >= 4);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

// Large files take whatever pages the sweep finds: scattered, and across the
// wrap from the last page to the first. They read back and survive a reboot.
void test_scattered_large_files() {
    RamFlash &f = new_flash(14); // 12 data pages
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    for (int i = 0; i < 12; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "p" + std::to_string(i), pattern(i, PAGE), 1, 4096));
    for (int i = 0; i < 12; i += 2) TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, ("p" + std::to_string(i)).c_str()));
    maintain_all();
    for (int round = 0; round < 4; round++) {
        const std::vector<uint8_t> data = pattern(100 + round, 5 * PAGE + 123);
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "big", data, 1, 4096));
        TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_RENDER, "big") == data);
        reboot(f);
        TEST_ASSERT_TRUE(read_file(MEDIA_BUCKET_RENDER, "big") == data);
        maintain_all();
    }
    check_no_leak(capture());
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
    TEST_ASSERT_EQUAL_UINT(0, f.map_violations);
}

void test_wear_telemetry() {
    RamFlash &f = new_flash(12);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    media_usage_t u;
    media_usage(&u);
    TEST_ASSERT_TRUE(u.dir_generation >= 1);
    const uint32_t e0 = u.erase_sectors;
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "a", pattern(1, 2 * PAGE), 1, 4096));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "a"));
    maintain_all();
    media_usage(&u);
    // written twice over fresh flash: the upload erased its 2 pages (fresh
    // flash is blank but not known to be) and maintenance erased them again
    TEST_ASSERT_TRUE(u.erase_sectors >= e0 + 2 * (PAGE / SECTOR));
    TEST_ASSERT_TRUE(u.erase_page_max >= PAGE / SECTOR);
    unsigned flash_total = 0;
    for (unsigned n : f.sector_erases) flash_total += n;
    TEST_ASSERT_EQUAL_UINT32(flash_total, u.erase_sectors); // counted from this mount, as the flash did
}

// ------------------------------------------------------------------ cost
//
// What a lookup and a listing cost with 56 live files in a log of 300
// records (the files were replaced 244 times and the log never compacted):
// flash read calls, records decoded, CRC bytes and key comparisons.

struct Cost {
    unsigned long reads, read_bytes, decodes, crc_bytes, key_cmps;
};

static Cost cost_of(RamFlash &f, const std::function<void()> &fn) {
    const unsigned r0 = f.reads;
    const unsigned long b0 = f.read_bytes;
    media_core_stats = media_core_stats_t{};
    fn();
    return Cost{f.reads - r0, f.read_bytes - b0, media_core_stats.rec_decodes, media_core_stats.crc_bytes,
                media_core_stats.key_cmps};
}

static std::string cost_key(int i) { return "font-" + std::to_string(100 + i * 11 % 56) + ".ttf"; }

static void cost_setup(RamFlash &f) {
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    for (int i = 0; i < 56; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, cost_key(i), pattern(i, 100), 1));
    std::mt19937 rng(3);
    for (int i = 0; i < 244; i++)
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, cost_key((int)(rng() % 56)), pattern(i, 100), 2));
}

void test_cost_lookup_and_listing() {
    RamFlash &f = new_flash(12);
    cost_setup(f);
    media_stat_t st;
    Cost sum{}, worst{};
    for (int i = 0; i < 56; i++) {
        const std::string key = cost_key(i);
        const Cost c = cost_of(f, [&] { TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_FONTS, key.c_str(), &st)); });
        sum.reads += c.reads, sum.read_bytes += c.read_bytes, sum.decodes += c.decodes, sum.crc_bytes += c.crc_bytes,
            sum.key_cmps += c.key_cmps;
        worst.reads = std::max(worst.reads, c.reads), worst.read_bytes = std::max(worst.read_bytes, c.read_bytes),
        worst.decodes = std::max(worst.decodes, c.decodes), worst.crc_bytes = std::max(worst.crc_bytes, c.crc_bytes),
        worst.key_cmps = std::max(worst.key_cmps, c.key_cmps);
    }
    printf("cost lookup (56 live, 300 records) mean: %.1f flash reads, %.0f bytes read, %.1f records decoded, "
           "%.0f CRC bytes, %.1f key comparisons\n",
           sum.reads / 56.0, sum.read_bytes / 56.0, sum.decodes / 56.0, sum.crc_bytes / 56.0, sum.key_cmps / 56.0);
    printf("cost lookup (56 live, 300 records) worst: %lu flash reads, %lu bytes read, %lu records decoded, "
           "%lu CRC bytes, %lu key comparisons\n",
           worst.reads, worst.read_bytes, worst.decodes, worst.crc_bytes, worst.key_cmps);
    const Cost miss = cost_of(f, [&] { TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_stat(MEDIA_BUCKET_FONTS, "nope", &st)); });
    printf("cost lookup of a missing key: %lu flash reads, %lu bytes read, %lu records decoded, %lu CRC bytes, "
           "%lu key comparisons\n",
           miss.reads, miss.read_bytes, miss.decodes, miss.crc_bytes, miss.key_cmps);
    int listed = 0;
    const Cost l = cost_of(f, [&] {
        for (media_err_t e = media_next(MEDIA_BUCKET_FONTS, nullptr, &st); e == MEDIA_OK;
             e = media_next(MEDIA_BUCKET_FONTS, st.key, &st))
            listed++;
    });
    TEST_ASSERT_EQUAL(56, listed);
    printf("cost listing of 56 files (57 calls): %lu flash reads, %lu bytes read, %lu records decoded, %lu CRC bytes, "
           "%lu key comparisons\n",
           l.reads, l.read_bytes, l.decodes, l.crc_bytes, l.key_cmps);
    // Before the index (same log): a lookup read the log up to its record
    // (mean 245.8 records decoded and CRC-checked, 27.8 flash reads, worst
    // 300 and 34), the listing 17100 records in 1938 flash reads.
    // Now: a binary search over the 56 live entries (at most 6 probes: 2^6 >
    // 56), the hit decoded once, no CRC, nothing read through read().
    TEST_ASSERT_EQUAL_UINT(0, worst.reads);
    TEST_ASSERT_EQUAL_UINT(0, worst.crc_bytes);
    TEST_ASSERT_EQUAL_UINT(1, worst.decodes);
    TEST_ASSERT_TRUE(worst.key_cmps <= 6);
    TEST_ASSERT_EQUAL_UINT(0, miss.reads + miss.decodes + miss.crc_bytes);
    TEST_ASSERT_TRUE(miss.key_cmps <= 6);
    // a listing is one search and one decode per file, not one scan per file
    TEST_ASSERT_EQUAL_UINT(0, l.reads);
    TEST_ASSERT_EQUAL_UINT(0, l.crc_bytes);
    TEST_ASSERT_EQUAL_UINT(56, l.decodes);
    TEST_ASSERT_TRUE(l.key_cmps <= 57 * 6);
    // the directory was mapped once and never again: the flash shows writes
    // through the mapping, as the device does
    TEST_ASSERT_EQUAL_UINT(1, f.dir_maps);
    TEST_ASSERT_EQUAL_UINT(1, f.dir_maps_live);
}

// Mount checks every record once: one CRC pass, the live entries decoded a
// second time (without CRC) for the free map.
void test_cost_mount() {
    RamFlash &f = new_flash(12);
    cost_setup(f);
    media_unmount();
    uint32_t log_bytes = 0;
    for (uint32_t off = 32; f.mem[off] != 0xFF; off += f.mem[off + 2] | f.mem[off + 3] << 8) log_bytes = off;
    const Cost c = cost_of(f, [&] { TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash)); });
    printf("cost mount (56 live, 300 records): %lu flash reads, %lu bytes read, %lu records decoded, %lu CRC bytes, "
           "%lu key comparisons\n",
           c.reads, c.read_bytes, c.decodes, c.crc_bytes, c.key_cmps);
    TEST_ASSERT_EQUAL_UINT(2, c.reads); // the two headers
    TEST_ASSERT_EQUAL_UINT(300 + 56, c.decodes);
    TEST_ASSERT_TRUE(c.crc_bytes <= 2 * 28 + log_bytes + 256); // headers + every record once
    TEST_ASSERT_TRUE(c.key_cmps <= 56 * 6);
}

// ----------------------------------------------------------------- index
//
// Lookups and listings come from the RAM index (offsets of the live
// records, sorted by bucket and key). These tests compare them with a
// std::map after every kind of directory change.

using Model = std::map<std::pair<int, std::string>, std::pair<uint32_t, uint32_t>>; // -> size, mtime

static void check_model(const Model &model) {
    media_stat_t st;
    auto it = model.begin();
    for (int b = 0; b < MEDIA_BUCKET_COUNT; b++) {
        const char *after = nullptr;
        std::string last;
        while (media_next((media_bucket_t)b, after, &st) == MEDIA_OK) {
            TEST_ASSERT_TRUE(it != model.end());
            TEST_ASSERT_EQUAL(it->first.first, b);
            TEST_ASSERT_EQUAL_STRING(it->first.second.c_str(), st.key);
            TEST_ASSERT_EQUAL_UINT32(it->second.first, st.size);
            TEST_ASSERT_EQUAL_UINT32(it->second.second, st.mtime);
            ++it;
            last = st.key;
            after = last.c_str();
        }
        TEST_ASSERT_TRUE(it == model.end() || it->first.first > b);
    }
    TEST_ASSERT_TRUE(it == model.end());
    for (const auto &kv : model) {
        TEST_ASSERT_EQUAL(MEDIA_OK, media_stat((media_bucket_t)kv.first.first, kv.first.second.c_str(), &st));
        TEST_ASSERT_EQUAL_STRING(kv.first.second.c_str(), st.key);
        TEST_ASSERT_EQUAL_UINT32(kv.second.first, st.size);
        TEST_ASSERT_EQUAL_UINT32(kv.second.second, st.mtime);
    }
}

// One lookup and one listing step for an arbitrary (bucket, key).
static void probe_model(const Model &model, int b, const std::string &key) {
    if (key.size() > MEDIA_KEY_MAX) return;
    media_stat_t st;
    const auto it = model.find({b, key});
    TEST_ASSERT_EQUAL(it == model.end() ? MEDIA_ERR_NOT_FOUND : MEDIA_OK, media_stat((media_bucket_t)b, key.c_str(), &st));
    if (it != model.end()) TEST_ASSERT_EQUAL_UINT32(it->second.second, st.mtime);
    const auto up = model.upper_bound({b, key});
    const media_err_t e = media_next((media_bucket_t)b, key.c_str(), &st);
    if (up == model.end() || up->first.first != b) {
        TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, e);
    } else {
        TEST_ASSERT_EQUAL(MEDIA_OK, e);
        TEST_ASSERT_EQUAL_STRING(up->first.second.c_str(), st.key);
    }
}

static media_err_t model_put(Model &model, int b, const std::string &key, uint32_t size, uint32_t mtime) {
    const media_err_t e = put((media_bucket_t)b, key, pattern(mtime, size), mtime);
    if (e == MEDIA_OK) model[{b, key}] = {size, mtime};
    return e;
}

// Keys that are prefixes of each other, the same keys in every bucket, the
// longest key, bytes >= 0x80, and a pool of plain ones.
static std::vector<std::string> index_keys() {
    std::vector<std::string> k = {"a", "ab", "abc", "abcd", "a/", "a/b", "a/b/c", "b", "ba", "z", "\xc3\xa9", "\xc3",
                                  "k", std::string(95, 'k'), std::string(96, 'k'), std::string(95, 'k') + "l", "~", " "};
    for (int i = 0; i < 30; i++) k.push_back("f" + std::to_string(i));
    return k;
}

static void run_index_model(bool dir_maps_are_copies) {
    RamFlash &f = new_flash(14);
    f.dir_maps_are_copies = dir_maps_are_copies;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    const std::vector<std::string> keys = index_keys();
    Model model;
    std::mt19937 rng(11);
    unsigned mounts = 1, snapshots_seen = 0;
    const unsigned headers0 = f.header_writes;
    for (int i = 0; i < 4000; i++) {
        const int b = (int)(rng() % 3);
        const std::string &key = keys[rng() % keys.size()];
        const int op = (int)(rng() % 20);
        if (op < 11) {
            const uint32_t sizes[] = {0, 0, 1, 50, 5000};
            const media_err_t e = model_put(model, b, key, sizes[rng() % 5], (uint32_t)i + 1);
            if (e != MEDIA_OK) {
                // only the store being full is acceptable; RENDER entries may have been evicted on the way
                TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, e);
            }
            for (auto it = model.begin(); it != model.end();) {
                media_stat_t st;
                if (it->first.first == MEDIA_BUCKET_RENDER &&
                    media_stat(MEDIA_BUCKET_RENDER, it->first.second.c_str(), &st) == MEDIA_ERR_NOT_FOUND)
                    it = model.erase(it);
                else ++it;
            }
        } else if (op < 17) {
            TEST_ASSERT_EQUAL(model.count({b, key}) ? MEDIA_OK : MEDIA_ERR_NOT_FOUND, media_delete((media_bucket_t)b, key.c_str()));
            model.erase({b, key});
        } else if (op < 19) {
            for (int k = 0; k < 3; k++) media_maintain(); // may write an idle snapshot
        } else if (rng() % 4 == 0) {
            reboot(f);
            mounts++;
        }
        probe_model(model, b, key);
        probe_model(model, (int)(rng() % 3), keys[rng() % keys.size()] + (rng() % 2 ? "" : "0"));
        const unsigned snaps = f.header_writes - headers0;
        if (snaps != snapshots_seen || i % 97 == 0) check_model(model); // right after every snapshot
        snapshots_seen = snaps;
    }
    check_model(model);
    reboot(f);
    mounts++;
    check_model(model);
    TEST_ASSERT_TRUE(snapshots_seen >= 5);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
    TEST_ASSERT_EQUAL_UINT(0, f.map_violations);
    TEST_ASSERT_EQUAL_UINT(1, f.dir_maps_live);
    if (dir_maps_are_copies) {
        // a mapping that is a copy has to be made again after every append
        TEST_ASSERT_TRUE(f.dir_maps > mounts + snapshots_seen + 1000);
    } else {
        // live mappings: one per mount and one per snapshot, never a remap
        TEST_ASSERT_EQUAL_UINT(mounts + snapshots_seen, f.dir_maps);
    }
}

void test_index_against_model() { run_index_model(false); }

// The host tool's flash (tools/media/mkmedia) maps by copying: the store
// notices that an append does not show and maps the half again.
void test_index_with_copied_mappings() { run_index_model(true); }

void test_index_prefix_keys_and_buckets() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    const std::vector<std::string> keys = {"abc", "a", "abcd", "ab", "a/", std::string(96, 'a'), std::string(95, 'a')};
    uint32_t t = 1;
    for (int b = 2; b >= 0; b--)
        for (const std::string &k : keys) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, b, k, (uint32_t)(b * 10 + k.size()), t++));
    check_model(model);
    // the same key in another bucket is another file
    media_stat_t st;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_IMAGES, "ab", &st));
    TEST_ASSERT_EQUAL_UINT32(12, st.size);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "ab"));
    model.erase({MEDIA_BUCKET_IMAGES, "ab"});
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "a"));
    model.erase({MEDIA_BUCKET_FONTS, "a"});
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, MEDIA_BUCKET_RENDER, "abc", 3, t++));
    check_model(model);
    // a prefix or an extension of a key is not that key
    for (int b = 0; b < 3; b++)
        for (const char *k : {"", "a", "ab", "abc", "abcd", "abcde", "a/", "a/b", "aa", "b", "\xff"}) {
            if (*k) probe_model(model, b, k);
        }
    probe_model(model, 0, std::string(94, 'a'));
    probe_model(model, 0, std::string(96, 'a'));
    reboot(f);
    check_model(model);
    for (int b = 0; b < 3; b++) probe_model(model, b, "ab");
    // the first and the last entry of the index, and an empty bucket
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "a/"));
    model.erase({MEDIA_BUCKET_FONTS, "a/"});
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_RENDER, "abcd"));
    model.erase({MEDIA_BUCKET_RENDER, "abcd"});
    for (const std::string &k : keys)
        if (model.erase({MEDIA_BUCKET_IMAGES, k})) TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, k.c_str()));
    check_model(model);
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_next(MEDIA_BUCKET_IMAGES, nullptr, &st));
    reboot(f);
    check_model(model);
}

// The active half and where its log ends, read from the flash image.
static uint32_t log_end(const RamFlash &f) {
    auto gen = [&](int h) { return (int32_t)(f.mem[h * PAGE + 8] | f.mem[h * PAGE + 9] << 8 | f.mem[h * PAGE + 10] << 16 | (uint32_t)f.mem[h * PAGE + 11] << 24); };
    auto valid = [&](int h) { return memcmp(&f.mem[h * PAGE], "MDIR", 4) == 0; };
    const int half = valid(1) && (!valid(0) || gen(1) - gen(0) > 0) ? 1 : 0;
    uint32_t off = half * PAGE + 32;
    while (f.mem[off] != 0xFF) off += f.mem[off + 2] | f.mem[off + 3] << 8;
    return off;
}

void test_index_after_snapshot_and_remount() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    for (int i = 0; i < 40; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, i % 3, "k" + std::to_string(i * 37 % 40), 10, t++));
    for (int i = 0; i < 40; i += 3) {
        TEST_ASSERT_EQUAL(MEDIA_OK, media_delete((media_bucket_t)(i % 3), ("k" + std::to_string(i * 37 % 40)).c_str()));
        model.erase({i % 3, "k" + std::to_string(i * 37 % 40)});
    }
    // replace until a put rolls the directory over (no maintenance in between)
    const unsigned h0 = f.header_writes;
    int n = 0;
    while (f.header_writes == h0) {
        TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, n % 3, "k" + std::to_string(1 + n % 7 * 3), 20, t++));
        TEST_ASSERT_TRUE(++n < 3000);
    }
    check_model(model); // the index of the new half, and the put that caused it
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "k1")); // kill ordinals of the new half
    model.erase({MEDIA_BUCKET_IMAGES, "k1"});
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, MEDIA_BUCKET_FONTS, "k4", 30, t++));
    check_model(model);
    reboot(f);
    check_model(model);
    maintain_all();
    check_model(model);
    reboot(f);
    check_model(model);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

void test_index_after_torn_tail() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    for (int i = 0; i < 12; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, i % 3, "t" + std::to_string(i), 100, t++));
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "t3", 200, t++));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "t4"));
    model.erase({MEDIA_BUCKET_IMAGES, "t4"});
    media_unmount();
    // a record cut short: the start of a DEL that would kill record 0 (t0), then of a PUT
    const uint32_t end = log_end(f);
    const uint8_t torn_del[] = {2, 0, 16, 0, 99, 0, 0, 0, 0, 0, 0, 0};
    memcpy(&f.mem[end], torn_del, sizeof torn_del);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    check_model(model);
    check_no_leak(capture());
    // the tail is not trusted: the next append goes to a snapshot, and the index follows
    const unsigned h0 = f.header_writes;
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "t-new", 10, t++));
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
    check_model(model);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "t0"));
    model.erase({MEDIA_BUCKET_FONTS, "t0"});
    check_model(model);
    reboot(f);
    check_model(model);
    // a record with a good frame and a bad CRC in the middle of valid ones:
    // everything from it on does not count
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 2, "t-last", 10, t++));
    Model before = model;
    const uint32_t cut = log_end(f);
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "gone1", pattern(1, 10), t++));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_RENDER, "t-last"));
    media_unmount();
    TEST_ASSERT_EQUAL_UINT8(10, f.mem[cut + 12]);
    f.mem[cut + 12] = 0; // the size of "gone1": its CRC no longer matches
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    check_model(before);
    check_no_leak(capture());
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(before, 0, "after", 10, t++));
    check_model(before);
    reboot(f);
    check_model(before);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

// 1000 live entries (the limit) and 1024 records (the limit of a half, and
// the size of the index).
void test_index_at_record_limit() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    char key[8];
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof key, "%04d", i * 619 % 1000); // not in key order
        TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, MEDIA_BUCKET_IMAGES, key, 0, t++));
    }
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, put(MEDIA_BUCKET_IMAGES, "one more", {}));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, put(MEDIA_BUCKET_IMAGES, "0500", {})); // a replace needs a free entry too
    check_model(model);
    const unsigned h0 = f.header_writes;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "0999")); // the last entry of the index
    model.erase({MEDIA_BUCKET_IMAGES, "0999"});
    for (int i = 0; i < 23; i++) {
        snprintf(key, sizeof key, "%04d", i * 41);
        TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, MEDIA_BUCKET_IMAGES, key, 0, t++));
    }
    TEST_ASSERT_EQUAL_UINT(h0, f.header_writes); // 1024 records, no snapshot yet
    check_model(model);
    probe_model(model, MEDIA_BUCKET_IMAGES, "0500x");
    probe_model(model, MEDIA_BUCKET_IMAGES, "9999");
    probe_model(model, MEDIA_BUCKET_FONTS, "0500");
    reboot(f); // mounts a half with exactly 1024 records
    check_model(model);
    TEST_ASSERT_EQUAL_UINT(h0, f.header_writes);
    // record 1025 does not fit: snapshot (999 records), then the DEL
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "0000")); // the first entry of the index
    model.erase({MEDIA_BUCKET_IMAGES, "0000"});
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
    check_model(model);
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, MEDIA_BUCKET_FONTS, "zzz", 0, t++));
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, MEDIA_BUCKET_RENDER, "a", 0, t++));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, put(MEDIA_BUCKET_IMAGES, "one more", {}));
    reboot(f);
    check_model(model);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

// ------------------------------------------------- idle directory snapshot

// media_maintain() writes a snapshot when at least 64 records are dead and
// they are at least twice the live ones, or when the half is nearly full.
void test_idle_snapshot_rule() {
    RamFlash &f = new_flash(12);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    for (int i = 0; i < 20; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "f" + std::to_string(i), 0, t++));
    maintain_all();
    const unsigned h0 = f.header_writes;
    // 63 dead records: nothing happens, however often the store is idle
    for (int i = 0; i < 63; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "f" + std::to_string(i % 5), 0, t++));
    maintain_all();
    TEST_ASSERT_FALSE(media_maintain());
    TEST_ASSERT_EQUAL_UINT(h0, f.header_writes);
    // the 64th: not while an upload is in progress
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "f0", 0, t++));
    media_writer_t *w = nullptr;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "upload", 0, 1, &w));
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(h0, f.header_writes);
    media_put_abort(w);
    // idle: the snapshot (no erase: the other half is blank), then the erase of the old half
    unsigned e0 = f.erases;
    TEST_ASSERT_TRUE(media_maintain());
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
    TEST_ASSERT_EQUAL_UINT(e0, f.erases);
    check_model(model);
    TEST_ASSERT_FALSE(media_maintain());
    TEST_ASSERT_EQUAL_UINT(e0 + 1, f.erases);
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes); // once
    check_model(model);

    // 40 live entries: 64 dead are not twice as many, 80 are. A delete makes two dead records.
    for (int i = 20; i < 40; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "g" + std::to_string(i), 0, t++));
    for (int i = 0; i < 79; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "f" + std::to_string(i % 20), 0, t++));
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "f7", 0, t++));
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(h0 + 2, f.header_writes);
    for (int i = 0; i < 16; i++) { // 16 put + delete pairs: 32 dead records, fewer than 64
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "tmp", {}));
        TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_RENDER, "tmp"));
    }
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(h0 + 2, f.header_writes);
    check_model(model);
    reboot(f);
    check_model(model);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

// Nearly full with few dead records: the snapshot is written when idle, so
// the next put or delete does not have to.
void test_idle_snapshot_before_full() {
    // by record count: 600 live, the half holds 1024 records
    {
        RamFlash &f = new_flash(8);
        TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
        char key[8];
        for (int i = 0; i < 600; i++) {
            snprintf(key, sizeof key, "%04d", i);
            TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, key, {}));
        }
        maintain_all();
        const unsigned h0 = f.header_writes;
        for (int i = 600; i < 1016; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "0007", {}, (uint32_t)i));
        maintain_all(); // 8 record slots left: not yet
        TEST_ASSERT_EQUAL_UINT(h0, f.header_writes);
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "0007", {}, 5000));
        TEST_ASSERT_EQUAL_UINT(h0, f.header_writes);
        maintain_all(); // 7 left
        TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
        // the appends that would have rolled over inside a put do not
        for (int i = 0; i < 20; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "0008", {}, (uint32_t)i));
        TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
        const State s = capture();
        TEST_ASSERT_EQUAL(600, (int)s.size());
        reboot(f);
        TEST_ASSERT_TRUE(capture() == s);
        TEST_ASSERT_EQUAL_UINT(0, f.violations);
        media_unmount();
    }
    // by bytes: 300 live entries with 96-byte keys (136 bytes each), less than 2 KB of log left
    {
        RamFlash &f = new_flash(8);
        TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
        auto key = [](int i) { return std::string(92, 'k') + std::to_string(1000 + i); };
        for (int i = 0; i < 300; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, key(i), {}));
        maintain_all();
        const unsigned h0 = f.header_writes;
        for (int i = 300; i < 466; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, key(i % 300), {}, (uint32_t)i));
        maintain_all(); // 32 + 466 * 136 + 2048 = 65456: not yet
        TEST_ASSERT_EQUAL_UINT(h0, f.header_writes);
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, key(5), {}, 5000));
        maintain_all();
        TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
        TEST_ASSERT_EQUAL(300, (int)capture().size());
        TEST_ASSERT_EQUAL_UINT(0, f.violations);
    }
}

// A power cut anywhere in an idle snapshot: the files never change.
void test_powercut_idle_snapshot() {
    auto setup = [] {
        base_files();
        for (int i = 0; i < 12; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "i" + std::to_string(i), pattern(50 + i, 100 + i), 5));
        for (int i = 0; i < 70; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "z", pattern(i, 10), 1000 + (uint32_t)i));
    };
    for (int by_bytes = 0; by_bytes < 2; by_bytes++) sweep(12, setup, [] { maintain_all(); }, false, by_bytes);
}

// ------------------------------------------------------- index memory
//
// The index is a heap block sized to the live entries, in steps of 16
// entries (32 bytes). It grows before anything is written for the put that
// needs the entry; when there is no memory the put fails and nothing else
// has happened.

static uint32_t used_now();

void test_index_memory_follows_entries() {
    RamFlash &f = new_flash(0x500000 / PAGE); // the device's partition
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL_UINT(6 * 80 + 32, alloc_bytes());
    Model model;
    uint32_t t = 1;
    auto key = [](int i) { return "font-" + std::to_string(100 + i * 11 % 56) + ".ttf"; };
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, key(i), 100, t++));
    TEST_ASSERT_EQUAL_UINT(32, index_bytes(f)); // 3 files
    for (int i = 3; i < 56; i++) {
        TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, i % 3, key(i), 100, t++));
        // one more step whenever the last one is full
        TEST_ASSERT_EQUAL_UINT(32 * ((i + 1 + 15) / 16), index_bytes(f));
    }
    TEST_ASSERT_EQUAL_UINT(128, index_bytes(f)); // 56 files
    check_model(model);
    // a replace takes no memory
    for (int i = 0; i < 30; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, i % 3, key(i), 50, t++));
    TEST_ASSERT_EQUAL_UINT(128, index_bytes(f));
    reboot(f);
    TEST_ASSERT_EQUAL_UINT(128, index_bytes(f));
    check_model(model);
    // deletes: the block shrinks with the next snapshot (here an idle one), and at mount
    for (int i = 0; i < 50; i++) {
        TEST_ASSERT_EQUAL(MEDIA_OK, media_delete((media_bucket_t)(i % 3), key(i).c_str()));
        model.erase({i % 3, key(i)});
    }
    TEST_ASSERT_EQUAL_UINT(128, index_bytes(f));
    const unsigned h0 = f.header_writes;
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
    TEST_ASSERT_EQUAL_UINT(32, index_bytes(f)); // 6 files
    check_model(model);
    reboot(f);
    TEST_ASSERT_EQUAL_UINT(32, index_bytes(f));
    check_model(model);
    check_no_leak(capture());
    media_unmount();

    // 1000 files (the entry limit), written and mounted
    RamFlash &g = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&g.flash));
    Model big;
    char k[8];
    for (int i = 0; i < 1000; i++) {
        snprintf(k, sizeof k, "%04d", i * 619 % 1000);
        TEST_ASSERT_EQUAL(MEDIA_OK, model_put(big, 1, k, 0, t++));
    }
    TEST_ASSERT_EQUAL_UINT(2016, index_bytes(g)); // 1008 entries
    reboot(g);
    TEST_ASSERT_EQUAL_UINT(2016, index_bytes(g));
    check_model(big);
}

void test_index_alloc_failure_put() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, i % 2, "k" + std::to_string(i), 3000, t++));
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, MEDIA_BUCKET_RENDER, "r", 40000, t++)); // evictable
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(64, index_bytes(f)); // 17 entries: the second step, 15 entries of room
    for (int i = 17; i < 32; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "k" + std::to_string(i), 0, t++));
    TEST_ASSERT_EQUAL_UINT(64, index_bytes(f)); // full
    const std::vector<uint8_t> mem = f.mem;
    const unsigned w0 = f.writes, e0 = f.erases;
    const uint32_t used0 = used_now();
    g_alloc_fail = true;
    // a new key needs an entry: refused before anything is written, also
    // before the eviction that this upload would need
    TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, put(MEDIA_BUCKET_FONTS, "new", pattern(1, 3000)));
    TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, put(MEDIA_BUCKET_IMAGES, "k0", pattern(1, 10))); // k0 is in FONTS
    TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, put(MEDIA_BUCKET_IMAGES, "huge", pattern(1, 5 * PAGE + 30000)));
    TEST_ASSERT_TRUE(g_alloc_failed >= 3);
    TEST_ASSERT_EQUAL_UINT(w0, f.writes);
    TEST_ASSERT_EQUAL_UINT(e0, f.erases);
    TEST_ASSERT_TRUE(mem == f.mem);
    TEST_ASSERT_EQUAL_UINT32(used0, used_now());
    TEST_ASSERT_EQUAL_UINT(64, index_bytes(f));
    check_model(model);
    // a replace and a delete need no entry: they work without memory
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "k0", 5000, t++));
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "k1", 0, t++));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "k2"));
    model.erase({0, "k2"});
    // and the delete made room for one new key
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "new", 3000, t++));
    TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, put(MEDIA_BUCKET_FONTS, "new2", pattern(1, 3000)));
    check_model(model);
    // two uploads in progress need an entry each
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "k4"));
    model.erase({0, "k4"});
    media_writer_t *w1 = nullptr, *w2 = nullptr;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "u1", 0, 1, &w1));
    TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, media_put_begin(MEDIA_BUCKET_FONTS, "u2", 0, 1, &w2));
    g_alloc_fail = false;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "u2", 0, 1, &w2));
    TEST_ASSERT_EQUAL_UINT(96, index_bytes(f));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_commit(w2, md5_of(1).data()));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_commit(w1, md5_of(1).data()));
    model[{0, "u1"}] = {0, 1};
    model[{0, "u2"}] = {0, 1};
    check_model(model);
    reboot(f);
    check_model(model);
    check_no_leak(capture());
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

// An upload that began as a replace (no entry needed, none could be had) and
// whose key was deleted before the commit: the commit adds an entry after
// all. The delete left it one, and no other upload can take that one (each
// counts the uploads in progress), so the commit does not need memory.
void test_index_replace_whose_key_was_deleted() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "k" + std::to_string(i), 0, t++));
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(32, index_bytes(f)); // full
    g_alloc_fail = true;
    media_writer_t *w = nullptr;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "k5", 6000, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_write(w, pattern(1, 6000).data(), 6000));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "k5"));
    model.erase({0, "k5"});
    TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, put(MEDIA_BUCKET_FONTS, "other", {})); // the entry k5 left is spoken for
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_commit(w, md5_of(1).data()));
    model[{0, "k5"}] = {6000, 1};
    TEST_ASSERT_EQUAL_UINT(32, index_bytes(f));
    check_model(model);
    g_alloc_fail = false;
    reboot(f);
    check_model(model);
    check_no_leak(capture());
}

// A snapshot gives back unused steps of the index, but not the entry of an
// upload in progress: its commit must not need memory.
void test_index_keeps_entry_of_upload_across_snapshot() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "k" + std::to_string(i), 0, t++));
    media_writer_t *w = nullptr;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_FONTS, "upload", 0, 1, &w));
    TEST_ASSERT_EQUAL_UINT(64, index_bytes(f)); // the second step, for the upload
    const unsigned h0 = f.header_writes;
    while (f.header_writes == h0) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "k3", 0, t++)); // until a put snapshots
    TEST_ASSERT_EQUAL_UINT(64, index_bytes(f));
    g_alloc_fail = true;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_commit(w, md5_of(1).data()));
    model[{0, "upload"}] = {0, 1};
    check_model(model);
    g_alloc_fail = false;
    reboot(f);
    check_model(model);
}

void test_index_alloc_failure_mount_and_trim() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    for (int i = 0; i < 40; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "k" + std::to_string(i), 10, t++));
    media_unmount();
    TEST_ASSERT_EQUAL_UINT(0, g_allocs.size());
    // mount: the free map, then the index
    for (long n = 0; n < 2; n++) {
        g_alloc_fail_in = n;
        TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, media_mount(&f.flash));
        TEST_ASSERT_EQUAL_UINT(0, g_allocs.size());
        TEST_ASSERT_EQUAL_UINT(0, f.dir_maps_live);
        media_stat_t st;
        TEST_ASSERT_EQUAL(MEDIA_ERR_UNAVAILABLE, media_stat(MEDIA_BUCKET_IMAGES, "k1", &st));
    }
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL_UINT(96, index_bytes(f)); // 40 entries
    check_model(model);
    // a snapshot that cannot get the smaller block keeps the larger one
    for (int i = 0; i < 36; i++) {
        TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, ("k" + std::to_string(i)).c_str()));
        model.erase({1, "k" + std::to_string(i)});
    }
    g_alloc_fail = true;
    const unsigned h0 = f.header_writes;
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
    TEST_ASSERT_EQUAL_UINT(96, index_bytes(f));
    check_model(model);
    g_alloc_fail = false;
    reboot(f);
    TEST_ASSERT_EQUAL_UINT(32, index_bytes(f));
    check_model(model);
}

// ---------------------------------------------------------- failed writes
//
// Transient flash errors (RamFlash::fault), no power cut: the store keeps
// running. What RAM says must stay what a reboot would find.

using Fault = RamFlash::Fault;

static bool in_directory(uint32_t off) { return off < MEDIA_DIR_PAGES * PAGE; }
static bool is_header(uint32_t off, uint32_t len) { return in_directory(off) && off % PAGE == 0 && len == 32; }
static bool is_record(uint32_t off, uint32_t len) { return in_directory(off) && !is_header(off, len); }

static std::vector<uint8_t> filled(char c, uint32_t size) { return std::vector<uint8_t>(size, (uint8_t)c); }

// The content of a file when every byte of it is the same: that byte, else 0.
static char content_of(media_bucket_t b, const char *key, uint32_t *size = nullptr) {
    media_err_t e;
    const std::vector<uint8_t> v = read_file(b, key, &e);
    if (size) *size = (uint32_t)v.size();
    if (e != MEDIA_OK || v.empty()) return 0;
    for (uint8_t c : v)
        if (c != v[0]) return 0;
    return (char)v[0];
}

static uint32_t used_now() {
    media_usage_t u;
    media_usage(&u);
    return u.used;
}

// D1: an idle snapshot that fails is not tried again on every call (each
// try erases the other half and writes all live records).
void test_idle_snapshot_failure_not_retried() {
    RamFlash &f = new_flash(8);
    bool fail_dir_map = false;
    f.fault = [&](const char *op, uint32_t off, uint32_t) {
        return fail_dir_map && !strcmp(op, "map") && in_directory(off) ? Fault::Fail : Fault::None;
    };
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    for (int i = 0; i < 20; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f" + std::to_string(i), 0, t++));
    for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f" + std::to_string(i % 5), 0, t++));
    fail_dir_map = true; // the snapshot fails after its records were written
    const unsigned e0 = f.erases, w0 = f.writes, h0 = f.header_writes;
    for (int i = 0; i < 100; i++) media_maintain();
    TEST_ASSERT_EQUAL_UINT(h0, f.header_writes);
    TEST_ASSERT_TRUE(f.erases - e0 <= 1); // the stale half once; the failed try itself erased nothing or once
    const unsigned w1 = f.writes;
    TEST_ASSERT_TRUE(w1 - w0 <= 20); // one try: the 20 live records
    for (int i = 0; i < 100; i++) TEST_ASSERT_FALSE(media_maintain());
    TEST_ASSERT_EQUAL_UINT(w1, f.writes);
    check_model(model);
    // the directory changes: one more try, and only one
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f3", 0, t++));
    const unsigned e2 = f.erases;
    for (int i = 0; i < 100; i++) media_maintain();
    TEST_ASSERT_EQUAL_UINT(h0, f.header_writes);
    TEST_ASSERT_TRUE(f.erases - e2 <= 1);
    TEST_ASSERT_TRUE(f.writes - w1 <= 1 + 20);
    check_model(model);
    // the flash works again: the next change brings the snapshot through
    fail_dir_map = false;
    for (int i = 0; i < 20; i++) TEST_ASSERT_FALSE(media_maintain());
    TEST_ASSERT_EQUAL_UINT(h0, f.header_writes);
    TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f4", 0, t++));
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
    check_model(model);
    reboot(f);
    check_model(model);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

// D2: the record of a commit reaches the flash but the commit fails. The
// record must not count after a reboot (its space is free again and may hold
// another upload by then).
static void run_failed_append_dropped(bool copies) {
    RamFlash &f = new_flash(3); // one data page
    f.dir_maps_are_copies = copies;
    int arm = 0;
    f.fault = [&](const char *op, uint32_t off, uint32_t len) {
        if (!arm) return Fault::None;
        if (copies) { // the record is written; mapping the half again to see it fails once
            if (strcmp(op, "map") || !in_directory(off)) return Fault::None;
            arm = 0;
            return Fault::Fail;
        }
        if (strcmp(op, "write") || !is_record(off, len)) return Fault::None;
        arm = 0;
        return Fault::FailDone; // programmed completely, reported as failed
    };
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "A", filled('1', 20000)));
    maintain_all();
    const unsigned h0 = f.header_writes;
    arm = 1;
    TEST_ASSERT_EQUAL(MEDIA_ERR_IO, put(MEDIA_BUCKET_IMAGES, "A", filled('2', 20000)));
    TEST_ASSERT_EQUAL(0, arm);
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes); // the snapshot that drops the record
    TEST_ASSERT_EQUAL('1', content_of(MEDIA_BUCKET_IMAGES, "A"));
    TEST_ASSERT_EQUAL_UINT32(5 * SECTOR, used_now());
    // another upload takes the space and is cut by a power loss before its commit
    media_writer_t *w = nullptr;
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_begin(MEDIA_BUCKET_IMAGES, "B", 28000, 1, &w));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_put_write(w, filled('B', 28000).data(), 28000));
    reboot(f);
    uint32_t size = 0;
    TEST_ASSERT_EQUAL('1', content_of(MEDIA_BUCKET_IMAGES, "A", &size));
    TEST_ASSERT_EQUAL_UINT32(20000, size);
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_stat(MEDIA_BUCKET_IMAGES, "B", &scratch_stat));
    check_no_leak(capture());
    // and without a reboot the store goes on
    arm = 1;
    TEST_ASSERT_EQUAL(MEDIA_ERR_IO, put(MEDIA_BUCKET_IMAGES, "C", filled('c', 100)));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "C", filled('C', 100)));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "A"));
    const State s = capture();
    TEST_ASSERT_EQUAL(1, (int)s.size());
    check_no_leak(s);
    reboot(f);
    TEST_ASSERT_TRUE(capture() == s);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

void test_failed_append_dropped_live_mapping() { run_failed_append_dropped(false); }
void test_failed_append_dropped_copied_mapping() { run_failed_append_dropped(true); }

// D2, second failure: the snapshot that would drop the record fails too. The
// space of the failed commit stays in use until a snapshot comes through, so
// whichever version a reboot finds has its own bytes. No background retries.
static void run_failed_append_unsettled(bool reboot_while_unsettled) {
    RamFlash &f = new_flash(3); // one data page: 16 sectors
    int state = 0;              // 1 armed, 2 everything fails
    unsigned attempts = 0;
    f.fault = [&](const char *op, uint32_t off, uint32_t len) {
        if (state == 1 && !strcmp(op, "write") && is_record(off, len)) {
            state = 2;
            return Fault::FailDone;
        }
        if (state == 2 && (!strcmp(op, "write") || !strcmp(op, "erase"))) {
            attempts++;
            return Fault::Fail;
        }
        return Fault::None;
    };
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "A", filled('1', 20000)));
    maintain_all();
    // 64 dead records (0-byte files): an idle snapshot is due, and stays due
    for (int i = 0; i < 65; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "z", {}));
    state = 1;
    TEST_ASSERT_EQUAL(MEDIA_ERR_IO, put(MEDIA_BUCKET_IMAGES, "A", filled('2', 20000)));
    TEST_ASSERT_EQUAL(2, state);
    TEST_ASSERT_EQUAL('1', content_of(MEDIA_BUCKET_IMAGES, "A"));
    TEST_ASSERT_EQUAL_UINT32(10 * SECTOR, used_now()); // both versions
    // idle: nothing is tried, although a snapshot is due
    const unsigned a0 = attempts;
    for (int i = 0; i < 50; i++) TEST_ASSERT_FALSE(media_maintain());
    TEST_ASSERT_EQUAL_UINT(a0, attempts);
    // an upload that would need the space of the failed commit: refused, one snapshot try
    media_writer_t *w = nullptr;
    TEST_ASSERT_EQUAL(MEDIA_ERR_IO, media_put_begin(MEDIA_BUCKET_IMAGES, "B", 7 * SECTOR, 1, &w));
    TEST_ASSERT_TRUE(attempts - a0 >= 1 && attempts - a0 <= 3);
    TEST_ASSERT_EQUAL(MEDIA_ERR_IO, media_delete(MEDIA_BUCKET_IMAGES, "A"));
    TEST_ASSERT_EQUAL('1', content_of(MEDIA_BUCKET_IMAGES, "A"));
    TEST_ASSERT_EQUAL_UINT32(10 * SECTOR, used_now());
    state = 0;
    if (reboot_while_unsettled) {
        // the record was complete on flash: the reboot finds version 2, whole
        reboot(f);
        uint32_t size = 0;
        TEST_ASSERT_EQUAL('2', content_of(MEDIA_BUCKET_IMAGES, "A", &size));
        TEST_ASSERT_EQUAL_UINT32(20000, size);
    } else {
        // the flash works again: the next upload brings the snapshot through,
        // version 2 is gone and its space is free
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "B", filled('B', 8 * SECTOR))); // 6 sectors were free
        TEST_ASSERT_EQUAL('1', content_of(MEDIA_BUCKET_IMAGES, "A"));
        TEST_ASSERT_EQUAL('B', content_of(MEDIA_BUCKET_IMAGES, "B"));
        TEST_ASSERT_EQUAL_UINT32(13 * SECTOR, used_now());
        maintain_all();
        reboot(f);
        TEST_ASSERT_EQUAL('1', content_of(MEDIA_BUCKET_IMAGES, "A"));
        TEST_ASSERT_EQUAL('B', content_of(MEDIA_BUCKET_IMAGES, "B"));
    }
    check_no_leak(capture());
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "A"));
    check_no_leak(capture());
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

void test_failed_append_and_snapshot_reboot() { run_failed_append_unsettled(true); }
void test_failed_append_and_snapshot_continue() { run_failed_append_unsettled(false); }

// A record that does not read back on a live mapping (nothing was
// programmed, or one bit is wrong, and the write reported success).
void test_append_readback_mismatch() {
    for (Fault kind : {Fault::Drop, Fault::Corrupt, Fault::FailHalf, Fault::Fail}) {
        RamFlash &f = new_flash(8);
        int arm = 0;
        f.fault = [&](const char *op, uint32_t off, uint32_t len) {
            if (!arm || strcmp(op, "write") || !is_record(off, len)) return Fault::None;
            arm = 0;
            return kind;
        };
        TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
        Model model;
        uint32_t t = 1;
        for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, i % 3, "k" + std::to_string(i), 100, t++));
        maintain_all();
        const unsigned maps0 = f.dir_maps;
        arm = 1;
        TEST_ASSERT_EQUAL(MEDIA_ERR_IO, put(MEDIA_BUCKET_FONTS, "k0", pattern(9, 50), 99)); // a replace
        check_model(model);
        arm = 1;
        TEST_ASSERT_EQUAL(MEDIA_ERR_IO, media_delete(MEDIA_BUCKET_IMAGES, "k1"));
        check_model(model);
        arm = 1;
        TEST_ASSERT_EQUAL(MEDIA_ERR_IO, put(MEDIA_BUCKET_RENDER, "new", pattern(9, 50), 99));
        check_model(model);
        TEST_ASSERT_TRUE(f.dir_maps > maps0); // the other half each time (and a second look at a record that did not show)
        const State s = capture();
        check_no_leak(s);
        // the store goes on, in RAM and on flash
        TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "k0", 70, t++));
        TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "k1"));
        model.erase({1, "k1"});
        check_model(model);
        reboot(f);
        check_model(model);
        check_no_leak(capture());
        TEST_ASSERT_EQUAL_UINT(0, f.violations);
        media_unmount();
    }
}

// The header of a snapshot, the commit point: every way its write can fail.
void test_snapshot_header_faults() {
    struct Case {
        const char *op; // the flash call that fails: the header write, or its read-back
        Fault kind;
        bool committed;
    };
    const Case cases[] = {
        {"write", Fault::Fail, false},    {"write", Fault::FailHalf, false}, {"write", Fault::Drop, false},
        {"write", Fault::Corrupt, false}, {"write", Fault::FailDone, true},  {"read", Fault::Fail, false},
    };
    for (const Case &c : cases)
        for (int idle = 0; idle < 2; idle++) {
            RamFlash &f = new_flash(8);
            int arm = 0;
            f.fault = [&](const char *op, uint32_t off, uint32_t len) {
                if (!arm || strcmp(op, c.op) || !is_header(off, len)) return Fault::None;
                arm = 0;
                return c.kind;
            };
            TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
            Model model;
            uint32_t t = 1;
            for (int i = 0; i < 20; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f" + std::to_string(i), 0, t++));
            maintain_all();
            if (idle) {
                // an idle snapshot: 64 dead records
                for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f" + std::to_string(i % 5), 0, t++));
                arm = 1;
                const unsigned w0 = f.writes;
                maintain_all();
                TEST_ASSERT_EQUAL(0, arm);
                const unsigned w1 = f.writes;
                for (int i = 0; i < 50; i++) media_maintain(); // no second try
                TEST_ASSERT_TRUE(f.writes - w0 <= 21);
                TEST_ASSERT_EQUAL_UINT(w1, f.writes);
            } else {
                // a snapshot inside a put: the half is full
                const unsigned h0 = f.header_writes;
                while (f.header_writes == h0) {
                    arm = 1;
                    const media_err_t e = model_put(model, 1, "f7", 0, t++);
                    if (f.header_writes == h0) {
                        TEST_ASSERT_EQUAL(MEDIA_OK, e);
                    } else {
                        TEST_ASSERT_EQUAL(c.committed ? MEDIA_OK : MEDIA_ERR_IO, e);
                    }
                    TEST_ASSERT_TRUE(t < 3000);
                }
                TEST_ASSERT_EQUAL(0, arm);
            }
            arm = 0;
            check_model(model);
            // what is appended now must be where a reboot looks for it
            TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "after", 10, t++));
            TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "f9"));
            model.erase({1, "f9"});
            check_model(model);
            reboot(f);
            check_model(model);
            check_no_leak(capture());
            maintain_all();
            TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 0, "later", 10, t++));
            reboot(f);
            check_model(model);
            TEST_ASSERT_EQUAL_UINT(0, f.violations);
            media_unmount();
        }
}

// The header read-back fails although the header is on flash: RAM stays on
// the old half, a reboot takes the new one, and they hold the same entries.
void test_snapshot_header_unknown_then_reboot() {
    RamFlash &f = new_flash(8);
    int arm = 0;
    f.fault = [&](const char *op, uint32_t off, uint32_t len) {
        if (!arm || strcmp(op, "read") || !is_header(off, len)) return Fault::None;
        arm = 0;
        return Fault::Fail;
    };
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    Model model;
    uint32_t t = 1;
    for (int i = 0; i < 20; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f" + std::to_string(i), 0, t++));
    maintain_all();
    for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f" + std::to_string(i % 5), 0, t++));
    arm = 1;
    maintain_all();
    TEST_ASSERT_EQUAL(0, arm);
    check_model(model);
    reboot(f);
    check_model(model);
    check_no_leak(capture());
}

// R1: a snapshot record that did not reach the flash as written must not
// become part of the trusted log.
void test_snapshot_records_verified() {
    for (Fault kind : {Fault::Corrupt, Fault::Drop}) {
        RamFlash &f = new_flash(8);
        int arm = 0, seen = 0;
        f.fault = [&](const char *op, uint32_t off, uint32_t len) {
            if (!arm || strcmp(op, "write") || !is_record(off, len)) return Fault::None;
            if (++seen != 7) return Fault::None; // the 7th record of the snapshot
            arm = 0;
            return kind;
        };
        TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
        Model model;
        uint32_t t = 1;
        for (int i = 0; i < 20; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, i % 3, "f" + std::to_string(i), 10, t++));
        maintain_all();
        for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f" + std::to_string(1 + i % 4 * 3), 10, t++));
        const unsigned h0 = f.header_writes;
        arm = 1;
        maintain_all();
        TEST_ASSERT_EQUAL(0, arm);
        TEST_ASSERT_EQUAL_UINT(h0, f.header_writes); // not committed
        check_model(model);
        const State s = capture();
        reboot(f);
        TEST_ASSERT_TRUE(capture() == s);
        // the next one works (it erases the half first)
        TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f1", 10, t++));
        maintain_all();
        TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes);
        check_model(model);
        reboot(f);
        check_model(model);
        check_no_leak(capture());
        TEST_ASSERT_EQUAL_UINT(0, f.violations);
        media_unmount();
    }
}

// R1: a log that is damaged after mount (a length that no record has). The
// walks stop there: no endless loop, no read outside the half; writes that
// need the walk fail with MEDIA_ERR_IO; a reboot keeps what precedes the damage.
void test_damaged_log_walks_stop() {
    for (int variant = 0; variant < 3; variant++) {
        RamFlash &f = new_flash(8);
        TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
        Model model;
        uint32_t t = 1;
        for (int i = 0; i < 10; i++) TEST_ASSERT_EQUAL(MEDIA_OK, model_put(model, 1, "f" + std::to_string(i), 0, t++));
        maintain_all();
        // the 4th record (f3)
        uint32_t off = 32;
        for (int i = 0; i < 3; i++) off += f.mem[off + 2] | f.mem[off + 3] << 8;
        const uint16_t bad = variant == 0 ? 0 : variant == 1 ? 0xFFFC : 8;
        f.mem[off + 2] = (uint8_t)bad;
        f.mem[off + 3] = (uint8_t)(bad >> 8);
        // lookups do not walk: they still answer
        media_stat_t st;
        TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_IMAGES, "f8", &st));
        int listed = 0;
        for (media_err_t e = media_next(MEDIA_BUCKET_IMAGES, nullptr, &st); e == MEDIA_OK && listed < 100;
             e = media_next(MEDIA_BUCKET_IMAGES, st.key, &st))
            listed++;
        TEST_ASSERT_TRUE(listed <= 10);
        // a delete needs the ordinal of f8: the walk stops at the damage
        const unsigned w0 = f.writes, e0 = f.erases;
        TEST_ASSERT_EQUAL(MEDIA_ERR_IO, media_delete(MEDIA_BUCKET_IMAGES, "f8"));
        TEST_ASSERT_EQUAL_UINT(w0, f.writes);
        // from here every append wants a snapshot first, which stops at the damage too
        TEST_ASSERT_EQUAL(MEDIA_ERR_IO, put(MEDIA_BUCKET_IMAGES, "new", {}));
        TEST_ASSERT_EQUAL(MEDIA_ERR_IO, media_delete(MEDIA_BUCKET_IMAGES, "f1"));
        for (int i = 0; i < 50; i++) media_maintain();
        TEST_ASSERT_TRUE(f.erases - e0 <= 3);
        TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_IMAGES, "f1", &st));
        // a reboot checks the CRCs again: the log ends before the damage
        reboot(f);
        const State s = capture();
        TEST_ASSERT_EQUAL(3, (int)s.size());
        TEST_ASSERT_TRUE(s.count({1, "f0"}) && s.count({1, "f1"}) && s.count({1, "f2"}));
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "new", {}));
        TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "f1"));
        reboot(f);
        TEST_ASSERT_EQUAL(3, (int)capture().size());
        TEST_ASSERT_EQUAL_UINT(0, f.violations);
        media_unmount();
    }
    // a key length that points outside the log, in the last record
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    for (int i = 0; i < 10; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "f" + std::to_string(i), {}));
    uint32_t off = 32;
    for (int i = 0; i < 9; i++) off += f.mem[off + 2] | f.mem[off + 3] << 8;
    f.mem[off + 10] = 0xFF;
    media_stat_t st;
    int listed = 0;
    for (media_err_t e = media_next(MEDIA_BUCKET_IMAGES, nullptr, &st); e == MEDIA_OK && listed < 100;
         e = media_next(MEDIA_BUCKET_IMAGES, st.key, &st))
        listed++;
    TEST_ASSERT_TRUE(listed <= 10);
    for (int i = 0; i < 10; i++) media_stat(MEDIA_BUCKET_IMAGES, ("f" + std::to_string(i)).c_str(), &st);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_IMAGES, "f4", &st));
}

// ------------------------------------------------------ crafted directories

static void craft_header(RamFlash &f, int half, uint32_t gen, uint32_t next_seq) {
    uint8_t *h = &f.mem[half * PAGE];
    memset(h, 0, 32);
    memcpy(h, "MDIR", 4);
    h[4] = 1;
    h[6] = 32;
    memcpy(h + 8, &gen, 4); // the tests run little-endian
    memcpy(h + 12, &next_seq, 4);
    h[16] = (uint8_t)(f.mem.size() / PAGE);
    h[18] = 2;
    const uint32_t crc = crc32_ref(h, 28);
    memcpy(h + 28, &crc, 4);
}

// A PUT of a 0-byte file at off (half 0); returns the offset after it.
static uint32_t craft_put(RamFlash &f, uint32_t off, int bucket, const std::string &key, uint32_t seq, uint32_t mtime) {
    const uint32_t len = ((36 + (uint32_t)key.size() + 3) & ~3u) + 4;
    uint8_t *p = &f.mem[off];
    memset(p, 0, len);
    p[0] = 1;
    p[1] = (uint8_t)bucket;
    p[2] = (uint8_t)len;
    p[3] = (uint8_t)(len >> 8);
    memcpy(p + 4, &seq, 4);
    p[8] = p[9] = 0xFF;
    p[10] = (uint8_t)key.size();
    memcpy(p + 16, &mtime, 4);
    memcpy(p + 20, md5_of(mtime).data(), 16);
    memcpy(p + 36, key.data(), key.size());
    const uint32_t crc = crc32_ref(p, len - 4);
    memcpy(p + len - 4, &crc, 4);
    return off + len;
}

// A log that holds a key twice (the store never writes one): listed once,
// the earlier record is the one found, nothing loops.
void test_duplicate_keys_in_log() {
    RamFlash &f = new_flash(8);
    uint32_t off = 32;
    off = craft_put(f, off, 0, "b", 1, 10);
    off = craft_put(f, off, 0, "dup", 2, 20);
    off = craft_put(f, off, 0, "a", 3, 30);
    off = craft_put(f, off, 0, "dup", 4, 40);
    off = craft_put(f, off, 1, "dup", 5, 50);
    off = craft_put(f, off, 0, "dup", 6, 60);
    off = craft_put(f, off, 0, "e", 7, 70);
    craft_header(f, 0, 1, 8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    media_stat_t st;
    std::vector<std::string> keys;
    for (media_err_t e = media_next(MEDIA_BUCKET_FONTS, nullptr, &st); e == MEDIA_OK && keys.size() < 50;
         e = media_next(MEDIA_BUCKET_FONTS, st.key, &st))
        keys.push_back(st.key);
    TEST_ASSERT_EQUAL(4, (int)keys.size());
    TEST_ASSERT_EQUAL_STRING("a", keys[0].c_str());
    TEST_ASSERT_EQUAL_STRING("b", keys[1].c_str());
    TEST_ASSERT_EQUAL_STRING("dup", keys[2].c_str());
    TEST_ASSERT_EQUAL_STRING("e", keys[3].c_str());
    // each delete ends the earliest one
    for (uint32_t mtime : {20u, 40u, 60u}) {
        TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_FONTS, "dup", &st));
        TEST_ASSERT_EQUAL_UINT32(mtime, st.mtime);
        TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_FONTS, "dup"));
        reboot(f);
    }
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_stat(MEDIA_BUCKET_FONTS, "dup", &st));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_IMAGES, "dup", &st));
    TEST_ASSERT_EQUAL_UINT32(50, st.mtime);
    const State s = capture();
    TEST_ASSERT_EQUAL(4, (int)s.size());
    check_no_leak(s);
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

// R3: a half with 1024 live records (mount takes it; the store itself stops
// at 1000 entries). No record can be added, and a snapshot would not help:
// MEDIA_ERR_NO_SPACE, nothing written or erased, nothing out of bounds.
void test_crafted_1024_live_records() {
    RamFlash &f = new_flash(8);
    uint32_t off = 32;
    char key[8];
    for (int i = 0; i < 1024; i++) {
        snprintf(key, sizeof key, "%04d", i * 619 % 1024);
        off = craft_put(f, off, MEDIA_BUCKET_IMAGES, key, (uint32_t)i + 1, (uint32_t)i + 1);
    }
    craft_header(f, 0, 1, 1025);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    media_stat_t st;
    int n = 0;
    std::string last;
    for (media_err_t e = media_next(MEDIA_BUCKET_IMAGES, nullptr, &st); e == MEDIA_OK; e = media_next(MEDIA_BUCKET_IMAGES, st.key, &st)) {
        TEST_ASSERT_TRUE(last < st.key);
        last = st.key;
        n++;
    }
    TEST_ASSERT_EQUAL(1024, n);
    maintain_all();
    const unsigned w0 = f.writes, e0 = f.erases;
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, media_delete(MEDIA_BUCKET_IMAGES, "0000"));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, media_delete(MEDIA_BUCKET_IMAGES, "1023"));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, put(MEDIA_BUCKET_IMAGES, "0500", {}));
    TEST_ASSERT_EQUAL(MEDIA_ERR_NO_SPACE, put(MEDIA_BUCKET_FONTS, "new", {}));
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(w0, f.writes);
    TEST_ASSERT_EQUAL_UINT(e0, f.erases);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_IMAGES, "0000", &st));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_IMAGES, "1023", &st));
    reboot(f);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_stat(MEDIA_BUCKET_IMAGES, "0512", &st));
    media_unmount();

    // 1023 live records: one slot, and a delete wins the next ones back
    RamFlash &g = new_flash(8);
    off = 32;
    for (int i = 0; i < 1023; i++) {
        snprintf(key, sizeof key, "%04d", i);
        off = craft_put(g, off, MEDIA_BUCKET_IMAGES, key, (uint32_t)i + 1, (uint32_t)i + 1);
    }
    craft_header(g, 0, 1, 1024);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&g.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "0000"));  // record 1024
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "1022"));  // snapshot: 1022 records, then the DEL
    TEST_ASSERT_EQUAL(MEDIA_ERR_NOT_FOUND, media_stat(MEDIA_BUCKET_IMAGES, "0000", &st));
    reboot(g);
    n = 0;
    for (media_err_t e = media_next(MEDIA_BUCKET_IMAGES, nullptr, &st); e == MEDIA_OK; e = media_next(MEDIA_BUCKET_IMAGES, st.key, &st)) n++;
    TEST_ASSERT_EQUAL(1021, n);
    TEST_ASSERT_EQUAL_UINT(0, g.violations);
}

// R2: at the entry limit a half has room for 24 more records. The idle rule
// does not snapshot for so little: only the put that finds the half full does.
void test_idle_snapshot_not_at_entry_limit() {
    RamFlash &f = new_flash(8);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    char key[8];
    for (int i = 0; i < 999; i++) {
        snprintf(key, sizeof key, "%04d", i);
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, key, {}));
    }
    maintain_all();
    const unsigned h0 = f.header_writes;
    unsigned idle = 0;
    for (int i = 0; i < 200; i++) {
        snprintf(key, sizeof key, "%04d", i % 7);
        TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, key, {}, 2 + (uint32_t)i));
        const unsigned h = f.header_writes;
        maintain_all();
        idle += f.header_writes - h;
    }
    TEST_ASSERT_EQUAL_UINT(0, idle);
    // 999 + 25 = 1024 records: replace 26 finds the half full, then every 25th (1000 + 24)
    TEST_ASSERT_EQUAL_UINT(7, f.header_writes - h0);
    TEST_ASSERT_EQUAL(999, (int)capture().size());
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
}

// Files held open across idle and in-put snapshots, replaced and deleted
// while open: the mappings stay, the space is freed on the last close.
void test_open_files_across_snapshots() {
    RamFlash &f = new_flash(12);
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "small", filled('s', 5000)));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "large", filled('L', 150000)));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_RENDER, "render", filled('r', 9000)));
    for (int i = 0; i < 10; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "x" + std::to_string(i), filled('x', 10)));
    media_file_t a{}, b{}, c{};
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "small", &a));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_IMAGES, "large", &b));
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_RENDER, "render", &c));
    auto all = [](const media_file_t &file, char ch, uint32_t size) {
        if (file.size != size) return false;
        for (uint32_t i = 0; i < size; i++)
            if (file.data[i] != (uint8_t)ch) return false;
        return true;
    };
    unsigned h0 = f.header_writes;
    for (int i = 0; i < 80; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "x" + std::to_string(i % 3), filled('y', 10)));
    maintain_all();
    TEST_ASSERT_EQUAL_UINT(h0 + 1, f.header_writes); // an idle snapshot with three files open
    TEST_ASSERT_TRUE(all(a, 's', 5000) && all(b, 'L', 150000) && all(c, 'r', 9000));
    TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_FONTS, "small", filled('S', 6000)));  // replaced while open
    TEST_ASSERT_EQUAL(MEDIA_OK, media_delete(MEDIA_BUCKET_IMAGES, "large"));           // deleted while open
    h0 = f.header_writes;
    for (int i = 0; i < 1100; i++) TEST_ASSERT_EQUAL(MEDIA_OK, put(MEDIA_BUCKET_IMAGES, "x" + std::to_string(i % 3), filled('z', 10)));
    TEST_ASSERT_TRUE(f.header_writes > h0); // snapshots inside puts, the dead files still open
    maintain_all();
    TEST_ASSERT_TRUE(all(a, 's', 5000) && all(b, 'L', 150000) && all(c, 'r', 9000));
    media_usage_t u;
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(2 * SECTOR + 3 * PAGE, u.reclaimable);
    media_file_t a2{};
    TEST_ASSERT_EQUAL(MEDIA_OK, media_open(MEDIA_BUCKET_FONTS, "small", &a2));
    TEST_ASSERT_TRUE(all(a2, 'S', 6000));
    media_close(&a2);
    media_close(&a);
    media_close(&b);
    media_close(&c);
    media_usage(&u);
    TEST_ASSERT_EQUAL_UINT32(0, u.reclaimable);
    TEST_ASSERT_EQUAL_UINT32(2 * SECTOR + 3 * SECTOR + 10 * SECTOR, u.used);
    TEST_ASSERT_EQUAL_UINT(0, f.maps_live);
    TEST_ASSERT_EQUAL_UINT(1, f.dir_maps_live);
    check_no_leak(capture());
    TEST_ASSERT_EQUAL_UINT(0, f.violations);
    TEST_ASSERT_EQUAL_UINT(0, f.map_violations);
}

// ------------------------------------------------------------ fault fuzzer
//
// Random operations with one flash call failing now and then (in one of
// three ways), against a model: an operation that reports an error did not
// happen, in RAM at once and on flash after a reboot; one that reports
// success did. Bounded: fixed seeds.

struct FuzzEnt {
    uint32_t seed, size, mtime;
};
using FuzzModel = std::map<std::pair<int, std::string>, FuzzEnt>;

// `content`: also compare every file's bytes (slow).
static std::string fuzz_check(const FuzzModel &m, bool content) {
    auto it = m.begin();
    media_stat_t st;
    uint32_t alloc = 0;
    for (int b = 0; b < MEDIA_BUCKET_COUNT; b++) {
        std::string after;
        int guard = 0;
        for (;;) {
            const media_err_t e = media_next((media_bucket_t)b, after.c_str(), &st);
            if (e != MEDIA_OK) {
                if (e != MEDIA_ERR_NOT_FOUND) return "listing error " + std::to_string((int)e);
                break;
            }
            if (++guard > 3000) return "listing does not end";
            if (it == m.end() || it->first.first != b || it->first.second != st.key) return std::string("listing has ") + st.key + ", the model does not";
            if (it->second.size != st.size || it->second.mtime != st.mtime) return std::string("size or mtime of ") + st.key;
            if (content) {
                media_file_t file{};
                if (media_open((media_bucket_t)b, st.key, &file) != MEDIA_OK) return std::string("open ") + st.key;
                const std::vector<uint8_t> want = pattern(it->second.seed, it->second.size);
                const bool same = file.size == want.size() && (file.size == 0 || memcmp(file.data, want.data(), file.size) == 0);
                media_close(&file);
                if (!same) return std::string("content of ") + st.key;
            }
            alloc += alloc_of(st.size);
            ++it;
            after = st.key;
        }
        if (it != m.end() && it->first.first <= b) return "the model has " + it->first.second + ", the listing does not";
    }
    media_usage_t u;
    media_usage(&u);
    if (u.used != alloc || u.reclaimable != 0) return "used " + std::to_string(u.used) + ", model " + std::to_string(alloc);
    return "";
}

static void fuzz_run(unsigned seed, uint32_t pages, int nops, Fault mode, bool copies, bool evict) {
    RamFlash &f = new_flash(pages);
    f.dir_maps_are_copies = copies;
    long countdown = -1;
    bool fired = false;
    const char *what = "";
    f.fault = [&](const char *op, uint32_t off, uint32_t) {
        if (countdown < 0 || countdown-- > 0) return Fault::None;
        fired = true;
        what = op;
        // a write that is lost in silence: only the directory is read back, file data is not
        if (strcmp(op, "write") || (mode == Fault::Drop && !in_directory(off))) return Fault::Fail;
        return mode;
    };
    TEST_ASSERT_EQUAL(MEDIA_OK, media_mount(&f.flash));
    std::mt19937 rng(seed);
    FuzzModel m;
    std::vector<std::string> keys;
    for (int i = 0; i < 24; i++) keys.push_back("k" + std::to_string(i) + std::string(i % 5 * 15, 'x'));
    char when[240];
    for (int i = 0; i < nops; i++) {
        const int b = evict ? (int)(rng() % 3) : (int)(rng() % 2);
        const std::string &key = keys[rng() % keys.size()];
        const int op = (int)(rng() % 20);
        countdown = rng() % 3 == 0 ? (long)(rng() % 40) : -1;
        fired = false;
        // and now and then an allocation of the store fails (the index growing)
        g_alloc_fail_in = rng() % 5 == 0 ? (long)(rng() % 2) : -1;
        const unsigned alloc_failed0 = g_alloc_failed;
        const char *name = "";
        media_err_t e = MEDIA_OK;
        bool unexpected = false;
        if (op < 11) {
            name = "put";
            const uint32_t sizes[] = {0, 1, 100, 5000, 5000, 20000, 70000};
            const uint32_t size = sizes[rng() % 7];
            // (put() of this file ends a failed write with media_put_abort)
            e = put((media_bucket_t)b, key, pattern((uint32_t)i + 1, size), (uint32_t)i + 1);
            if (e == MEDIA_OK) m[{b, key}] = FuzzEnt{(uint32_t)i + 1, size, (uint32_t)i + 1};
            else unexpected = !fired && g_alloc_failed == alloc_failed0 && e != MEDIA_ERR_NO_SPACE;
        } else if (op < 16) {
            name = "delete";
            e = media_delete((media_bucket_t)b, key.c_str());
            if (e == MEDIA_OK) m.erase({b, key});
            else unexpected = (!fired && !(e == MEDIA_ERR_NOT_FOUND && !m.count({b, key}))) || e == MEDIA_ERR_UNAVAILABLE;
        } else if (op < 19) {
            name = "maintain";
            for (int k = 0; k < 4; k++) media_maintain();
        } else {
            name = "reboot";
            countdown = -1;
            g_alloc_fail_in = -1;
            reboot(f);
        }
        g_alloc_fail_in = -1;
        if (g_alloc_failed != alloc_failed0) {
            fired = true;
            what = "alloc";
        }
        const bool was_fired = fired;
        countdown = -1;
        if (evict) { // RENDER entries may have been evicted
            for (auto it = m.begin(); it != m.end();) {
                media_stat_t st;
                if (it->first.first == MEDIA_BUCKET_RENDER &&
                    media_stat(MEDIA_BUCKET_RENDER, it->first.second.c_str(), &st) == MEDIA_ERR_NOT_FOUND)
                    it = m.erase(it);
                else ++it;
            }
        }
        std::string problem = unexpected ? "an error without a fault" : fuzz_check(m, was_fired || i % 16 == 0);
        if (problem.empty() && was_fired && rng() % 4 == 0) { // and the flash agrees
            reboot(f);
            problem = fuzz_check(m, true);
            if (!problem.empty()) problem += " (after a reboot)";
        }
        if (problem.empty() && (f.violations || f.map_violations || f.dir_maps_live != 1)) problem = "violations or directory mappings";
        if (!problem.empty()) {
            snprintf(when, sizeof when, "mode %d copies %d evict %d seed %u op %d %s(%d/%s) -> %d%s%s: %s", (int)mode, copies,
                     evict, seed, i, name, b, key.c_str(), (int)e, was_fired ? ", fault in " : "", was_fired ? what : "",
                     problem.c_str());
            TEST_FAIL_MESSAGE(when);
        }
    }
    media_unmount();
}

static void fuzz_mode(Fault mode) {
    for (int copies = 0; copies < 2; copies++)
        for (int evict = 0; evict < 2; evict++)
            for (unsigned seed = 1; seed <= 3; seed++) fuzz_run(seed, evict ? 10 : 40, 1000, mode, copies, evict);
}

void test_fault_fuzz_nothing_written() { fuzz_mode(Fault::Fail); }
void test_fault_fuzz_half_written() { fuzz_mode(Fault::FailHalf); }
void test_fault_fuzz_all_written() { fuzz_mode(Fault::FailDone); }
void test_fault_fuzz_silent() { fuzz_mode(Fault::Drop); }

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_format_and_header);
    RUN_TEST(test_put_open_close_delete);
    RUN_TEST(test_same_key_shares_mapping);
    RUN_TEST(test_zero_size_and_writer_rules);
    RUN_TEST(test_bad_keys);
    RUN_TEST(test_replace_and_delete_while_open);
    RUN_TEST(test_usage_numbers);
    RUN_TEST(test_full_store_no_space);
    RUN_TEST(test_render_eviction_order);
    RUN_TEST(test_concurrent_reservations);
    RUN_TEST(test_open_table_limit);
    RUN_TEST(test_next_order_and_bounds);
    RUN_TEST(test_max_sizes);
    RUN_TEST(test_directory_limit);
    RUN_TEST(test_rollover_many_records);
    RUN_TEST(test_small_packing_fragmentation_compaction);
    RUN_TEST(test_preerase_and_sync_erase);
    RUN_TEST(test_powercut_put);
    RUN_TEST(test_powercut_replace);
    RUN_TEST(test_powercut_delete);
    RUN_TEST(test_powercut_eviction);
    RUN_TEST(test_powercut_snapshot_rollover);
    RUN_TEST(test_powercut_compaction);
    RUN_TEST(test_wear_frames);
    RUN_TEST(test_wear_history);
    RUN_TEST(test_wear_mixed);
    RUN_TEST(test_cursor_seed_varies);
    RUN_TEST(test_scattered_large_files);
    RUN_TEST(test_wear_telemetry);
    RUN_TEST(test_cost_lookup_and_listing);
    RUN_TEST(test_cost_mount);
    RUN_TEST(test_index_against_model);
    RUN_TEST(test_index_with_copied_mappings);
    RUN_TEST(test_index_prefix_keys_and_buckets);
    RUN_TEST(test_index_after_snapshot_and_remount);
    RUN_TEST(test_index_after_torn_tail);
    RUN_TEST(test_index_at_record_limit);
    RUN_TEST(test_idle_snapshot_rule);
    RUN_TEST(test_idle_snapshot_before_full);
    RUN_TEST(test_powercut_idle_snapshot);
    RUN_TEST(test_idle_snapshot_not_at_entry_limit);
    RUN_TEST(test_idle_snapshot_failure_not_retried);
    RUN_TEST(test_failed_append_dropped_live_mapping);
    RUN_TEST(test_failed_append_dropped_copied_mapping);
    RUN_TEST(test_failed_append_and_snapshot_reboot);
    RUN_TEST(test_failed_append_and_snapshot_continue);
    RUN_TEST(test_append_readback_mismatch);
    RUN_TEST(test_snapshot_header_faults);
    RUN_TEST(test_snapshot_header_unknown_then_reboot);
    RUN_TEST(test_snapshot_records_verified);
    RUN_TEST(test_damaged_log_walks_stop);
    RUN_TEST(test_duplicate_keys_in_log);
    RUN_TEST(test_crafted_1024_live_records);
    RUN_TEST(test_open_files_across_snapshots);
    RUN_TEST(test_fault_fuzz_nothing_written);
    RUN_TEST(test_fault_fuzz_half_written);
    RUN_TEST(test_fault_fuzz_all_written);
    RUN_TEST(test_fault_fuzz_silent);
    RUN_TEST(test_index_memory_follows_entries);
    RUN_TEST(test_index_alloc_failure_put);
    RUN_TEST(test_index_replace_whose_key_was_deleted);
    RUN_TEST(test_index_keeps_entry_of_upload_across_snapshot);
    RUN_TEST(test_index_alloc_failure_mount_and_trim);
    return UNITY_END();
}
