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

void setUp() {}
void tearDown() {
    media_unmount();
    g_flashes.clear();
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
    return UNITY_END();
}
