//
// Native tests for media_image_check (src/ui/media_image_check.c): the LVGL 9
// .bin rasters of the `images` bucket are accepted only when the header and
// the size agree exactly (MediaImage.h), and the icon key builder.
//
#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/ui/media_image_check.c"

void setUp() {}
void tearDown() {}

// A raster as the IDE writes it: header + pixel bytes (content irrelevant).
static std::vector<uint8_t> raster(uint8_t cf, uint16_t w, uint16_t h, uint16_t stride, uint32_t pixels,
                                   uint8_t magic = 0x19, uint16_t flags = 0, uint16_t reserved = 0) {
    std::vector<uint8_t> v(12 + pixels, 0xA5);
    v[0] = magic;
    v[1] = cf;
    v[2] = (uint8_t)flags;
    v[3] = (uint8_t)(flags >> 8);
    v[4] = (uint8_t)w;
    v[5] = (uint8_t)(w >> 8);
    v[6] = (uint8_t)h;
    v[7] = (uint8_t)(h >> 8);
    v[8] = (uint8_t)stride;
    v[9] = (uint8_t)(stride >> 8);
    v[10] = (uint8_t)reserved;
    v[11] = (uint8_t)(reserved >> 8);
    return v;
}

static media_image_err_t check(const std::vector<uint8_t>& v, media_image_info_t* info = nullptr) {
    return media_image_check(v.data(), (uint32_t)v.size(), info);
}

// --- valid rasters ----------------------------------------------------------

void test_valid_a8() {
    const auto v = raster(0x0E, 48, 40, 48, 48 * 40);
    media_image_info_t info;
    memset(&info, 0xFF, sizeof info);
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(v, &info));
    TEST_ASSERT_EQUAL_UINT(48, info.w);
    TEST_ASSERT_EQUAL_UINT(40, info.h);
    TEST_ASSERT_EQUAL_UINT(48, info.stride);
    TEST_ASSERT_EQUAL_HEX8(0x0E, info.cf);
    TEST_ASSERT_EQUAL_UINT32(12, info.data_offset);
    TEST_ASSERT_EQUAL_UINT32(48 * 40, info.data_size);
}

void test_valid_rgb565a8() {
    // RGB565 plane (stride w * 2) then an A8 plane (stride w).
    const auto v = raster(0x14, 32, 24, 64, 32 * 24 * 2 + 32 * 24);
    media_image_info_t info;
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(v, &info));
    TEST_ASSERT_EQUAL_UINT(32, info.w);
    TEST_ASSERT_EQUAL_UINT(24, info.h);
    TEST_ASSERT_EQUAL_UINT(64, info.stride);
    TEST_ASSERT_EQUAL_HEX8(0x14, info.cf);
    TEST_ASSERT_EQUAL_UINT32(32 * 24 * 3, info.data_size);
}

void test_valid_rgb565() {
    const auto v = raster(0x12, 20, 10, 40, 20 * 10 * 2);
    media_image_info_t info;
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(v, &info));
    TEST_ASSERT_EQUAL_UINT(40, info.stride);
    TEST_ASSERT_EQUAL_UINT32(400, info.data_size);
}

void test_valid_extremes() {
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(raster(0x0E, 1, 1, 1, 1)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(raster(0x14, 1, 1, 2, 3)));
    // 1024 x 1024 RGB565A8: 3 MiB of pixels, no overflow.
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(raster(0x14, 1024, 1024, 2048, 1024u * 1024u * 3u)));
    // Odd widths: no row padding.
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(raster(0x0E, 7, 3, 7, 21)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(raster(0x14, 7, 3, 14, 63)));
}

void test_info_may_be_null() {
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(raster(0x0E, 4, 4, 4, 16), nullptr));
}

// --- refused ----------------------------------------------------------------

void test_truncated_header() {
    const auto v = raster(0x0E, 4, 4, 4, 16);
    for (uint32_t n = 0; n < 12; n++) TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_TRUNCATED, media_image_check(v.data(), n, nullptr));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_TRUNCATED, media_image_check(nullptr, 100, nullptr));
}

void test_bad_magic() {
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_MAGIC, check(raster(0x0E, 4, 4, 4, 16, 0x00)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_MAGIC, check(raster(0x0E, 4, 4, 4, 16, 0x18)));
    // A PNG is not a raster.
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0x0D, 'I', 'H', 'D', 'R'};
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_MAGIC, check(png));
}

void test_bad_cf() {
    // RGB888, ARGB8888, XRGB8888, I8, L8, A4, RGB565_SWAPPED: refused.
    for (uint8_t cf : {0x00, 0x0F, 0x10, 0x11, 0x13, 0x0A, 0x06, 0x0C, 0x1B})
        TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_CF, check(raster(cf, 4, 4, 8, 32)));
}

void test_bad_flags() {
    // LV_IMAGE_FLAGS_PREMULTIPLIED (0x01), COMPRESSED (0x08), user bits.
    for (uint16_t f : {0x0001, 0x0008, 0x0100, 0x8000})
        TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_FLAGS, check(raster(0x0E, 4, 4, 4, 16, 0x19, f)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_FLAGS, check(raster(0x0E, 4, 4, 4, 16, 0x19, 0, 1)));  // reserved
}

void test_bad_dimensions() {
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_DIM, check(raster(0x0E, 0, 4, 0, 0)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_DIM, check(raster(0x0E, 4, 0, 4, 0)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_DIM, check(raster(0x0E, 1025, 1, 1025, 1025)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_DIM, check(raster(0x0E, 1, 1025, 1, 1025)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_DIM, check(raster(0x12, 0xFFFF, 1, 0xFFFE, 16)));
}

void test_bad_stride() {
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_STRIDE, check(raster(0x0E, 4, 4, 8, 32)));    // A8: w
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_STRIDE, check(raster(0x0E, 4, 4, 0, 16)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_STRIDE, check(raster(0x12, 4, 4, 4, 16)));    // RGB565: w * 2
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_STRIDE, check(raster(0x14, 4, 4, 12, 48)));   // RGB565A8: w * 2
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_STRIDE, check(raster(0x14, 5, 4, 12, 64)));   // padded rows
}

void test_bad_size() {
    // One byte short, one byte long, the A8 plane missing, a second header.
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_SIZE, check(raster(0x0E, 4, 4, 4, 15)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_SIZE, check(raster(0x0E, 4, 4, 4, 17)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_SIZE, check(raster(0x14, 4, 4, 8, 32)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_SIZE, check(raster(0x12, 4, 4, 8, 32 + 12)));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_ERR_SIZE, check(raster(0x12, 4, 4, 8, 0)));  // header only
    // Truncated upload of a valid raster: every shorter length is refused.
    const auto v = raster(0x14, 6, 5, 12, 6 * 5 * 3);
    for (uint32_t n = 12; n < v.size(); n++) TEST_ASSERT_NOT_EQUAL(MEDIA_IMAGE_OK, media_image_check(v.data(), n, nullptr));
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(v));
}

void test_header_is_little_endian() {
    // w = 0x0102 = 258: a big-endian reader would see 513.
    const auto v = raster(0x0E, 0x0102, 2, 0x0102, 0x0102 * 2);
    TEST_ASSERT_EQUAL_HEX8(0x02, v[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, v[5]);
    media_image_info_t info;
    TEST_ASSERT_EQUAL(MEDIA_IMAGE_OK, check(v, &info));
    TEST_ASSERT_EQUAL_UINT(258, info.w);
}

void test_err_strings() {
    for (int e = MEDIA_IMAGE_OK; e <= MEDIA_IMAGE_ERR_SIZE; e++) {
        const char* s = media_image_err_str((media_image_err_t)e);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        TEST_ASSERT_TRUE(strcmp(s, "?") != 0);
    }
}

// --- icon names and keys ----------------------------------------------------

void test_name_ok() {
    TEST_ASSERT_TRUE(media_image_name_ok("rain"));
    TEST_ASSERT_TRUE(media_image_name_ok("a"));
    TEST_ASSERT_TRUE(media_image_name_ok("Weather_sun-2"));
    TEST_ASSERT_TRUE(media_image_name_ok("-"));
    TEST_ASSERT_TRUE(media_image_name_ok(std::string(31, 'x').c_str()));
    TEST_ASSERT_FALSE(media_image_name_ok(std::string(32, 'x').c_str()));
    TEST_ASSERT_FALSE(media_image_name_ok(""));
    TEST_ASSERT_FALSE(media_image_name_ok(nullptr));
    for (const char* bad : {"a.b", "a b", "a/b", "../x", "caf\xc3\xa9", "a\x01", "rain.svg", "a+b"})
        TEST_ASSERT_FALSE_MESSAGE(media_image_name_ok(bad), bad);
}

void test_icon_key() {
    char out[64];
    TEST_ASSERT_TRUE(media_image_icon_key(out, sizeof out, "rain", 48, 48));
    TEST_ASSERT_EQUAL_STRING("ico-rain-48x48.bin", out);
    TEST_ASSERT_TRUE(media_image_icon_key(out, sizeof out, "sun_2-x", 32, 20));
    TEST_ASSERT_EQUAL_STRING("ico-sun_2-x-32x20.bin", out);
    TEST_ASSERT_TRUE(media_image_icon_key(out, sizeof out, "a", 1, 1024));
    TEST_ASSERT_EQUAL_STRING("ico-a-1x1024.bin", out);
    // The longest key: 31-char name, 1024x1024 (49 chars) fits a media key.
    const std::string n31(31, 'n');
    TEST_ASSERT_TRUE(media_image_icon_key(out, sizeof out, n31.c_str(), 1024, 1024));
    TEST_ASSERT_EQUAL_STRING(("ico-" + n31 + "-1024x1024.bin").c_str(), out);
    TEST_ASSERT_EQUAL_UINT(49, strlen(out));
}

void test_icon_key_invalid() {
    char out[64];
    for (const char* bad : {"", "a.b", "a b", "a/b", "rain.svg"}) {
        strcpy(out, "junk");
        TEST_ASSERT_FALSE_MESSAGE(media_image_icon_key(out, sizeof out, bad, 48, 48), bad);
        TEST_ASSERT_EQUAL_STRING("", out);
    }
    TEST_ASSERT_FALSE(media_image_icon_key(out, sizeof out, nullptr, 48, 48));
    TEST_ASSERT_FALSE(media_image_icon_key(out, sizeof out, std::string(32, 'x').c_str(), 48, 48));
    TEST_ASSERT_FALSE(media_image_icon_key(out, sizeof out, "rain", 0, 48));
    TEST_ASSERT_FALSE(media_image_icon_key(out, sizeof out, "rain", 48, 0));
    TEST_ASSERT_FALSE(media_image_icon_key(out, sizeof out, "rain", 1025, 48));
    TEST_ASSERT_FALSE(media_image_icon_key(out, sizeof out, "rain", 48, 1025));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(media_image_icon_key(nullptr, 64, "rain", 48, 48));
    TEST_ASSERT_FALSE(media_image_icon_key(out, 0, "rain", 48, 48));
}

void test_icon_key_overflow() {
    // "ico-rain-48x48.bin" is 18 chars: needs cap 19.
    char out[19];
    memset(out, 'Z', sizeof out);
    TEST_ASSERT_TRUE(media_image_icon_key(out, 19, "rain", 48, 48));
    TEST_ASSERT_EQUAL_STRING("ico-rain-48x48.bin", out);
    for (size_t cap = 1; cap < 19; cap++) {
        char small[19];
        memset(small, 'Z', sizeof small);
        TEST_ASSERT_FALSE(media_image_icon_key(small, cap, "rain", 48, 48));
        TEST_ASSERT_EQUAL_CHAR('\0', small[0]);
        TEST_ASSERT_EQUAL_CHAR('Z', small[cap]);  // nothing written past cap
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_a8);
    RUN_TEST(test_valid_rgb565a8);
    RUN_TEST(test_valid_rgb565);
    RUN_TEST(test_valid_extremes);
    RUN_TEST(test_info_may_be_null);
    RUN_TEST(test_truncated_header);
    RUN_TEST(test_bad_magic);
    RUN_TEST(test_bad_cf);
    RUN_TEST(test_bad_flags);
    RUN_TEST(test_bad_dimensions);
    RUN_TEST(test_bad_stride);
    RUN_TEST(test_bad_size);
    RUN_TEST(test_header_is_little_endian);
    RUN_TEST(test_err_strings);
    RUN_TEST(test_name_ok);
    RUN_TEST(test_icon_key);
    RUN_TEST(test_icon_key_invalid);
    RUN_TEST(test_icon_key_overflow);
    return UNITY_END();
}
