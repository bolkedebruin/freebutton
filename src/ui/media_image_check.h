// Checks for the raster objects of the media store's `images` bucket (LVGL 9
// .bin, format in src/ui/MediaImage.h) and the icon raster key builder.
// Pure C: no LVGL, no ESP-IDF, so the native tests cover it
// (test/test_media_image).
#ifndef MEDIA_IMAGE_CHECK_H
#define MEDIA_IMAGE_CHECK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_IMAGE_HEADER_SIZE 12u
#define MEDIA_IMAGE_MAGIC 0x19u        /* LV_IMAGE_HEADER_MAGIC */
#define MEDIA_IMAGE_CF_A8 0x0Eu        /* LV_COLOR_FORMAT_A8 */
#define MEDIA_IMAGE_CF_RGB565 0x12u    /* LV_COLOR_FORMAT_RGB565 */
#define MEDIA_IMAGE_CF_RGB565A8 0x14u  /* LV_COLOR_FORMAT_RGB565A8 */
#define MEDIA_IMAGE_DIM_MAX 1024u      /* largest width / height */
#define MEDIA_IMAGE_NAME_MAX 31u       /* icon name, [A-Za-z0-9_-] */

typedef enum {
    MEDIA_IMAGE_OK = 0,
    MEDIA_IMAGE_ERR_TRUNCATED, /* shorter than the header */
    MEDIA_IMAGE_ERR_MAGIC,
    MEDIA_IMAGE_ERR_CF,        /* not A8, RGB565 or RGB565A8 */
    MEDIA_IMAGE_ERR_FLAGS,     /* flags or reserved not 0 (compressed, premultiplied, ...) */
    MEDIA_IMAGE_ERR_DIM,       /* w or h 0 or above MEDIA_IMAGE_DIM_MAX */
    MEDIA_IMAGE_ERR_STRIDE,    /* not w (A8) or w * 2 (RGB565, RGB565A8) */
    MEDIA_IMAGE_ERR_SIZE,      /* not exactly header + pixel bytes */
} media_image_err_t;

typedef struct {
    uint16_t w, h, stride;
    uint8_t cf;
    uint32_t data_offset; /* MEDIA_IMAGE_HEADER_SIZE */
    uint32_t data_size;   /* pixel bytes (both planes of RGB565A8) */
} media_image_info_t;

/* Validate a raster of `size` bytes at `buf` (the whole object). The header
 * is read byte-wise, little endian. Fills *out (may be NULL) on success. */
media_image_err_t media_image_check(const uint8_t *buf, uint32_t size, media_image_info_t *out);

/* Short reason for a log line. */
const char *media_image_err_str(media_image_err_t e);

/* An icon name: 1..MEDIA_IMAGE_NAME_MAX of [A-Za-z0-9_-]. NULL is not. */
bool media_image_name_ok(const char *name);

/* "ico-<name>-<w>x<h>.bin" into out (NUL-terminated). False, with out ""
 * when cap > 0, when the name is not valid, w or h is 0 or above
 * MEDIA_IMAGE_DIM_MAX, or the key does not fit cap. */
bool media_image_icon_key(char *out, size_t cap, const char *name, uint16_t w, uint16_t h);

#ifdef __cplusplus
}
#endif

#endif /* MEDIA_IMAGE_CHECK_H */
