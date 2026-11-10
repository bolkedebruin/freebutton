//
// Native tests for the streaming .bin font reader (src/ui/fonts/font_bin.c).
//
// data/fonts/*.bin and test/font_reference/mnt_*.c come from the same lv_font_conv
// run (tools/fonts/generatefonts.sh), so every glyph must match exactly:
// cmap, metrics, the compressed bitmap bytes, the decoded pixels and kerning.
// The compiled fonts are #included as plain data (test/mocks/lvgl.h supplies
// the types); their bitmaps are decoded with a separate transcription of
// LVGL's own decompressor, so the two decoders check each other.
//
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "lvgl.h" // test/mocks/lvgl.h

#include "../../src/ui/fonts/font_bin.c"

// Each generated file uses the same static names, so give each a namespace.
namespace c_medium_14 {
#include "../font_reference/mnt_medium_14.c"
}
namespace c_medium_24 {
#include "../font_reference/mnt_medium_24.c"
}
namespace c_medium_32 {
#include "../font_reference/mnt_medium_32.c"
}
namespace c_medium_48 {
#include "../font_reference/mnt_medium_48.c"
}
namespace c_medium_72 {
#include "../font_reference/mnt_medium_72.c"
}
namespace c_semibold_14 {
#include "../font_reference/mnt_semibold_14.c"
}
namespace c_semibold_18 {
#include "../font_reference/mnt_semibold_18.c"
}

void setUp() {}
void tearDown() {}

// ---- in-memory font file ---------------------------------------------------

struct MemFile {
    std::vector<uint8_t> data;
    size_t reads = 0;
};

static bool mem_read(void *ctx, uint32_t pos, void *dst, uint32_t len) {
    auto *m = static_cast<MemFile *>(ctx);
    m->reads++;
    if (pos > m->data.size() || len > m->data.size() - pos) return false;
    memcpy(dst, m->data.data() + pos, len);
    return true;
}

static size_t g_allocs = 0;
static void *count_alloc(size_t n) {
    g_allocs++;
    return malloc(n);
}
static void count_free(void *p) {
    g_allocs--;
    free(p);
}

static bool load_file(const std::string &name, MemFile &out) {
    // pio runs tests from the project root; fall back to this file's location.
    std::string here = __FILE__;
    here = here.substr(0, here.find_last_of('/') + 1);
    for (const std::string &path : {"data/fonts/" + name, here + "../../data/fonts/" + name}) {
        FILE *fp = fopen(path.c_str(), "rb");
        if (!fp) continue;
        fseek(fp, 0, SEEK_END);
        out.data.resize(static_cast<size_t>(ftell(fp)));
        fseek(fp, 0, SEEK_SET);
        size_t got = fread(out.data.data(), 1, out.data.size(), fp);
        fclose(fp);
        return got == out.data.size();
    }
    return false;
}

static bool open_font(MemFile &mem, font_bin_t &f) {
    const font_bin_io_t io = {mem_read, count_alloc, count_free, &mem};
    return font_bin_open(&f, &io);
}

// ---- reference side: the compiled fmt_txt font -------------------------------

static const lv_font_fmt_txt_dsc_t *dsc_of(const lv_font_t *font) {
    return static_cast<const lv_font_fmt_txt_dsc_t *>(font->dsc);
}

// LVGL's get_glyph_dsc_id() over the compiled cmaps.
static uint32_t ref_glyph_id(const lv_font_fmt_txt_dsc_t *d, uint32_t cp) {
    for (uint32_t i = 0; i < d->cmap_num; i++) {
        const lv_font_fmt_txt_cmap_t &c = d->cmaps[i];
        uint32_t rcp = cp - c.range_start;
        if (rcp >= c.range_length) continue;
        switch (c.type) {
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY: return c.glyph_id_start + rcp;
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL:
                return c.glyph_id_start + static_cast<const uint8_t *>(c.glyph_id_ofs_list)[rcp];
            case LV_FONT_FMT_TXT_CMAP_SPARSE_TINY:
            case LV_FONT_FMT_TXT_CMAP_SPARSE_FULL:
                for (uint32_t k = 0; k < c.list_length; k++) {
                    if (c.unicode_list[k] != rcp) continue;
                    if (c.type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY) return c.glyph_id_start + k;
                    return c.glyph_id_start + static_cast<const uint16_t *>(c.glyph_id_ofs_list)[k];
                }
                return 0;
        }
    }
    return 0;
}

// LVGL's get_kern_value() over the compiled tables.
static int8_t ref_kern(const lv_font_fmt_txt_dsc_t *d, uint32_t l, uint32_t r) {
    if (!d->kern_dsc) return 0;
    if (d->kern_classes) {
        auto *k = static_cast<const lv_font_fmt_txt_kern_classes_t *>(d->kern_dsc);
        uint8_t lc = k->left_class_mapping[l], rc = k->right_class_mapping[r];
        if (lc == 0 || rc == 0) return 0;
        return k->class_pair_values[(lc - 1) * k->right_class_cnt + (rc - 1)];
    }
    // Pairs: build a map once per font (pair lists are small).
    static std::map<const void *, std::map<std::pair<uint32_t, uint32_t>, int8_t>> cache;
    auto &m = cache[d];
    if (m.empty()) {
        auto *k = static_cast<const lv_font_fmt_txt_kern_pair_t *>(d->kern_dsc);
        for (uint32_t i = 0; i < k->pair_cnt; i++) {
            uint32_t a, b;
            if (k->glyph_ids_size == 0) {
                auto *ids = static_cast<const uint8_t *>(k->glyph_ids);
                a = ids[2 * i], b = ids[2 * i + 1];
            } else {
                auto *ids = static_cast<const uint16_t *>(k->glyph_ids);
                a = ids[2 * i], b = ids[2 * i + 1];
            }
            m[{a, b}] = k->values[i];
        }
    }
    auto it = m.find({l, r});
    return it == m.end() ? 0 : it->second;
}

// Transcription of LVGL's decompress()/rle_next() (lv_font_fmt_txt.c),
// producing raw pixel values instead of opacities.
struct RefRle {
    const uint8_t *in;
    uint32_t rdp = 0;
    uint8_t bpp, prev = 0, count = 0;
    enum { SINGLE, REPEATED, COUNTER } state = SINGLE;

    uint8_t bits(uint8_t len) {
        uint8_t v = 0;
        for (uint8_t i = 0; i < len; i++, rdp++) v = (v << 1) | ((in[rdp >> 3] >> (7 - (rdp & 7))) & 1);
        return v;
    }
    uint8_t next() {
        uint8_t ret = 0;
        if (state == SINGLE) {
            bool first = rdp == 0;
            ret = bits(bpp);
            if (!first && prev == ret) {
                count = 0;
                state = REPEATED;
            }
            prev = ret;
        } else if (state == REPEATED) {
            uint8_t v = bits(1);
            count++;
            if (v == 1) {
                ret = prev;
                if (count == 11) {
                    count = bits(6);
                    if (count != 0) state = COUNTER;
                    else {
                        ret = prev = bits(bpp);
                        state = SINGLE;
                    }
                }
            } else {
                ret = prev = bits(bpp);
                state = SINGLE;
            }
        } else {
            ret = prev;
            if (--count == 0) {
                ret = prev = bits(bpp);
                state = SINGLE;
            }
        }
        return ret;
    }
};

static std::vector<uint8_t> ref_decode(const lv_font_fmt_txt_dsc_t *d, const lv_font_fmt_txt_glyph_dsc_t &g) {
    const uint32_t w = g.box_w, h = g.box_h;
    std::vector<uint8_t> out(w * h);
    const uint8_t *in = d->glyph_bitmap + g.bitmap_index;
    if (d->bitmap_format == 0) {
        for (uint32_t i = 0, p = 0; i < w * h; i++, p += d->bpp) {
            uint8_t v = 0;
            for (uint32_t b = 0; b < d->bpp; b++) v = (v << 1) | ((in[(p + b) >> 3] >> (7 - ((p + b) & 7))) & 1);
            out[i] = v;
        }
        return out;
    }
    RefRle rle;
    rle.in = in;
    rle.bpp = static_cast<uint8_t>(d->bpp);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t v = rle.next();
            if (d->bitmap_format == 1 && y > 0) v ^= out[(y - 1) * w + x];
            out[y * w + x] = v;
        }
    }
    return out;
}

// The bitmap part of a .bin glyph record, shifted back to byte alignment.
static std::vector<uint8_t> bin_bitmap_bytes(const MemFile &mem, const font_bin_t &f, const font_bin_glyph_t &g,
                                             size_t n) {
    uint32_t skip = f.adv_bits + 2u * f.xy_bits + 2u * f.wh_bits;
    std::vector<uint8_t> out(n, 0);
    for (size_t bit = 0; bit < n * 8; bit++) {
        uint32_t src = skip + static_cast<uint32_t>(bit);
        if (src >= g.len * 8u) break; // past the record: padding zeros
        uint8_t b = (mem.data[g.pos + (src >> 3)] >> (7 - (src & 7))) & 1;
        out[bit >> 3] |= static_cast<uint8_t>(b << (7 - (bit & 7)));
    }
    return out;
}

// ---- per-font comparison -----------------------------------------------------

struct FontCase {
    const char *bin;
    const lv_font_t *font;
    size_t glyph_count; // compiled glyph_dsc entries
    size_t bitmap_size; // compiled glyph_bitmap bytes
};

#define FONT_CASE(ns, sym) {#sym ".bin", &ns::sym, sizeof(ns::glyph_dsc) / sizeof(ns::glyph_dsc[0]), sizeof(ns::glyph_bitmap)}

static const FontCase kFonts[] = {
    FONT_CASE(c_medium_14, mnt_medium_14),     FONT_CASE(c_medium_24, mnt_medium_24),
    FONT_CASE(c_medium_32, mnt_medium_32),     FONT_CASE(c_medium_48, mnt_medium_48),
    FONT_CASE(c_medium_72, mnt_medium_72),     FONT_CASE(c_semibold_14, mnt_semibold_14),
    FONT_CASE(c_semibold_18, mnt_semibold_18),
};

static void compare_font(const FontCase &fc) {
    MemFile mem;
    char msg[160];
    snprintf(msg, sizeof(msg), "load %s", fc.bin);
    TEST_ASSERT_TRUE_MESSAGE(load_file(fc.bin, mem), msg);

    font_bin_t f;
    TEST_ASSERT_TRUE_MESSAGE(open_font(mem, f), fc.bin);
    const lv_font_fmt_txt_dsc_t *d = dsc_of(fc.font);

    // font level
    TEST_ASSERT_EQUAL_MESSAGE(fc.glyph_count, f.glyph_count, fc.bin);
    TEST_ASSERT_EQUAL_MESSAGE(d->bpp, f.bpp, fc.bin);
    TEST_ASSERT_EQUAL_MESSAGE(d->bitmap_format, f.compression, fc.bin);
    TEST_ASSERT_EQUAL_MESSAGE(d->cmap_num, f.cmap_count, fc.bin);
    TEST_ASSERT_EQUAL_MESSAGE(fc.font->line_height, f.ascent - f.descent, fc.bin);
    TEST_ASSERT_EQUAL_MESSAGE(fc.font->base_line, -f.descent, fc.bin);
    TEST_ASSERT_EQUAL_MESSAGE(fc.font->underline_thickness, f.underline_thickness, fc.bin);
    // fonts.c derives the position (lost in .bin) as -round(size / 10)
    TEST_ASSERT_EQUAL_MESSAGE(fc.font->underline_position, -(int32_t)(f.font_size + 5) / 10, fc.bin);
    TEST_ASSERT_EQUAL_MESSAGE(d->kern_dsc != nullptr, f.kern_pos != 0, fc.bin);
    if (d->kern_dsc) TEST_ASSERT_EQUAL_MESSAGE(d->kern_scale, f.kern_scale, fc.bin);

    // every glyph
    size_t bitmaps = 0;
    std::vector<uint8_t> out;
    for (uint16_t gid = 1; gid < f.glyph_count; gid++) {
        const lv_font_fmt_txt_glyph_dsc_t &cg = d->glyph_dsc[gid];
        font_bin_glyph_t g;
        snprintf(msg, sizeof(msg), "%s gid %u", fc.bin, gid);
        TEST_ASSERT_TRUE_MESSAGE(font_bin_glyph(&f, gid, &g), msg);
        TEST_ASSERT_EQUAL_MESSAGE(cg.adv_w, g.adv_w, msg);
        TEST_ASSERT_EQUAL_MESSAGE(cg.box_w, g.box_w, msg);
        TEST_ASSERT_EQUAL_MESSAGE(cg.box_h, g.box_h, msg);
        TEST_ASSERT_EQUAL_MESSAGE(cg.ofs_x, g.ofs_x, msg);
        TEST_ASSERT_EQUAL_MESSAGE(cg.ofs_y, g.ofs_y, msg);
        if (cg.box_w == 0 || cg.box_h == 0) continue;

        // Same compressed stream: compiled bytes up to the next glyph's bitmap.
        size_t end = fc.bitmap_size;
        for (uint16_t n = gid + 1; n < f.glyph_count; n++) {
            if (d->glyph_dsc[n].box_w && d->glyph_dsc[n].box_h) {
                end = d->glyph_dsc[n].bitmap_index;
                break;
            }
        }
        size_t n = end - cg.bitmap_index;
        std::vector<uint8_t> raw = bin_bitmap_bytes(mem, f, g, n);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(d->glyph_bitmap + cg.bitmap_index, raw.data(), n, msg);

        // Same pixels: our decoder on the .bin vs LVGL's on the compiled data.
        // Use a padded stride and check the padding stays untouched.
        const uint32_t stride = g.box_w + 3;
        out.assign(stride * g.box_h, 0xEE);
        TEST_ASSERT_TRUE_MESSAGE(font_bin_decode(&f, &g, out.data(), stride, false), msg);
        std::vector<uint8_t> ref = ref_decode(d, cg);
        for (uint32_t y = 0; y < g.box_h; y++) {
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&ref[y * g.box_w], &out[y * stride], g.box_w, msg);
            for (uint32_t x = g.box_w; x < stride; x++) TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xEE, out[y * stride + x], msg);
        }

        // A8 output is the LVGL opacity table applied to the same pixels.
        std::vector<uint8_t> a8(g.box_w * g.box_h);
        TEST_ASSERT_TRUE_MESSAGE(font_bin_decode(&f, &g, a8.data(), g.box_w, true), msg);
        for (size_t i = 0; i < a8.size(); i++) TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref[i] * 17, a8[i], msg);
        bitmaps++;
    }

    // cmap: Basic Multilingual Plane plus the MDI icon block, hits and misses
    size_t mapped = 0;
    std::vector<font_bin_cache_entry_t> entries(f.glyph_count); // by glyph id, for the kerning check
    auto check_range = [&](uint32_t from, uint32_t to) {
        for (uint32_t cp = from; cp < to; cp++) {
            uint32_t want = ref_glyph_id(d, cp);
            uint16_t got = font_bin_glyph_id(&f, cp);
            if (want != got) {
                snprintf(msg, sizeof(msg), "%s U+%04X", fc.bin, cp);
                TEST_ASSERT_EQUAL_MESSAGE(want, got, msg);
            }
            if (want) {
                mapped++;
                font_bin_cache_t fresh = {};
                TEST_ASSERT_TRUE(font_bin_lookup_entry(&f, &fresh, cp, &entries[want]));
            }
        }
    };
    check_range(0, 0x10000);
    check_range(0xF0000, 0xF2000);
    TEST_ASSERT_EQUAL_MESSAGE(f.glyph_count - 1, mapped, fc.bin); // every glyph reachable

    // kerning: every glyph pair
    size_t kerned = 0;
    for (uint16_t l = 1; l < f.glyph_count; l++) {
        for (uint16_t r = 1; r < f.glyph_count; r++) {
            int8_t want = ref_kern(d, l, r);
            int8_t got = font_bin_kern_raw(&f, l, r);
            if (want != got) {
                snprintf(msg, sizeof(msg), "%s kern %u,%u", fc.bin, l, r);
                TEST_ASSERT_EQUAL_MESSAGE(want, got, msg);
            }
            if (want) kerned++;
            // the cached-class path gives the same, scaled
            int32_t scaled = font_bin_kern_entries(&f, &entries[l], &entries[r]);
            if (scaled != font_bin_kern(&f, l, r)) {
                snprintf(msg, sizeof(msg), "%s kern entries %u,%u", fc.bin, l, r);
                TEST_ASSERT_EQUAL_MESSAGE(font_bin_kern(&f, l, r), scaled, msg);
            }
        }
    }

    printf("%-20s %4u glyphs, %4zu bitmaps, %5zu code points, %6zu kerned pairs (kern format %u), "
           "index %zu bytes\n",
           fc.bin, f.glyph_count - 1, bitmaps, mapped, kerned, f.kern_format,
           sizeof(font_bin_t) + f.cmap_count * sizeof(font_bin_cmap_t));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, kerned, fc.bin);

    font_bin_close(&f);
}

void test_medium_14() { compare_font(kFonts[0]); }
void test_medium_24() { compare_font(kFonts[1]); }
void test_medium_32() { compare_font(kFonts[2]); }
void test_medium_48() { compare_font(kFonts[3]); }
void test_medium_72() { compare_font(kFonts[4]); }
void test_semibold_14() { compare_font(kFonts[5]); }
void test_semibold_18() { compare_font(kFonts[6]); }

// ---- cache, robustness -------------------------------------------------------

void test_cache_matches_direct_lookup() {
    MemFile mem;
    TEST_ASSERT_TRUE(load_file("mnt_medium_24.bin", mem));
    font_bin_t f;
    TEST_ASSERT_TRUE(open_font(mem, f));

    font_bin_cache_t cache = {};
    const std::u32string text = U"The quick brown fox °C \U000F0590 jumps over the lazy dog € 0123456789 \U000F0335";
    for (int pass = 0; pass < 3; pass++) {
        for (char32_t cp : text) {
            uint16_t gid = 0;
            font_bin_glyph_t g, direct;
            bool found = font_bin_lookup(&f, &cache, cp, &gid, &g);
            uint16_t want = font_bin_glyph_id(&f, cp);
            TEST_ASSERT_EQUAL(want != 0, found);
            if (!found) continue;
            TEST_ASSERT_EQUAL(want, gid);
            TEST_ASSERT_TRUE(font_bin_glyph(&f, gid, &direct));
            TEST_ASSERT_EQUAL_MEMORY(&direct, &g, sizeof(g));
            TEST_ASSERT_TRUE(font_bin_glyph_cached(&f, &cache, gid, &g));
            TEST_ASSERT_EQUAL_MEMORY(&direct, &g, sizeof(g));
        }
    }

    // A cached letter costs no file reads; a missing one is cached as a miss.
    uint16_t gid;
    font_bin_glyph_t g;
    TEST_ASSERT_TRUE(font_bin_lookup(&f, &cache, 'o', &gid, &g));
    TEST_ASSERT_FALSE(font_bin_lookup(&f, &cache, 0x4E2D, &gid, &g)); // CJK, not in the font
    size_t before = mem.reads;
    TEST_ASSERT_TRUE(font_bin_lookup(&f, &cache, 'o', &gid, &g));
    TEST_ASSERT_FALSE(font_bin_lookup(&f, &cache, 0x4E2D, &gid, &g));
    TEST_ASSERT_EQUAL(before, mem.reads);
    font_bin_close(&f);
}

void test_truncated_and_bad_files_fail_cleanly() {
    MemFile full;
    TEST_ASSERT_TRUE(load_file("mnt_medium_14.bin", full));

    g_allocs = 0;
    for (size_t cut : {0, 4, 47, 48, 200, 1500, 3000, 20000}) {
        MemFile mem;
        mem.data.assign(full.data.begin(), full.data.begin() + cut);
        font_bin_t f;
        TEST_ASSERT_FALSE(open_font(mem, f));
        TEST_ASSERT_EQUAL(0, g_allocs); // failed open frees the cmap index
    }

    MemFile bad = full;
    bad.data[4] = 'x'; // label "head" -> "xead"
    font_bin_t f;
    TEST_ASSERT_FALSE(open_font(bad, f));

    // File cut after open (e.g. replaced underneath): reads fail, no crash.
    MemFile mem = full;
    TEST_ASSERT_TRUE(open_font(mem, f));
    font_bin_glyph_t g;
    uint16_t gid = font_bin_glyph_id(&f, 'W');
    TEST_ASSERT_TRUE(font_bin_glyph(&f, gid, &g));
    std::vector<uint8_t> out(g.box_w * g.box_h);
    TEST_ASSERT_FALSE(font_bin_decode(&f, &g, out.data(), g.box_w - 1, false)); // stride too small
    mem.data.resize(g.pos + 4);
    TEST_ASSERT_FALSE(font_bin_decode(&f, &g, out.data(), g.box_w, false));
    font_bin_glyph_t other;
    TEST_ASSERT_FALSE(font_bin_glyph(&f, static_cast<uint16_t>(f.glyph_count - 1), &other));
    TEST_ASSERT_FALSE(font_bin_glyph(&f, 0, &other));
    TEST_ASSERT_FALSE(font_bin_glyph(&f, f.glyph_count, &other));
    font_bin_close(&f);
    TEST_ASSERT_EQUAL(0, g_allocs);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_medium_14);
    RUN_TEST(test_medium_24);
    RUN_TEST(test_medium_32);
    RUN_TEST(test_medium_48);
    RUN_TEST(test_medium_72);
    RUN_TEST(test_semibold_14);
    RUN_TEST(test_semibold_18);
    RUN_TEST(test_cache_matches_direct_lookup);
    RUN_TEST(test_truncated_and_bad_files_fail_cleanly);
    return UNITY_END();
}
