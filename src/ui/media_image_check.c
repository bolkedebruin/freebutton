// Raster checks and the icon key builder (media_image_check.h).
#include "media_image_check.h"

#include <stdio.h>
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

media_image_err_t media_image_check(const uint8_t *buf, uint32_t size, media_image_info_t *out) {
    if (!buf || size < MEDIA_IMAGE_HEADER_SIZE) return MEDIA_IMAGE_ERR_TRUNCATED;
    /* lv_image_header_t, little endian: magic, cf, flags(16), w, h, stride,
     * reserved(16). */
    const uint8_t magic = buf[0];
    const uint8_t cf = buf[1];
    const uint16_t flags = le16(buf + 2);
    const uint16_t w = le16(buf + 4);
    const uint16_t h = le16(buf + 6);
    const uint16_t stride = le16(buf + 8);
    const uint16_t reserved = le16(buf + 10);

    if (magic != MEDIA_IMAGE_MAGIC) return MEDIA_IMAGE_ERR_MAGIC;
    uint32_t bpp; /* bytes per pixel of the first plane */
    uint32_t alpha_plane;
    switch (cf) {
        case MEDIA_IMAGE_CF_A8: bpp = 1; alpha_plane = 0; break;
        case MEDIA_IMAGE_CF_RGB565: bpp = 2; alpha_plane = 0; break;
        case MEDIA_IMAGE_CF_RGB565A8: bpp = 2; alpha_plane = 1; break;
        default: return MEDIA_IMAGE_ERR_CF;
    }
    if (flags != 0 || reserved != 0) return MEDIA_IMAGE_ERR_FLAGS;
    if (w == 0 || h == 0 || w > MEDIA_IMAGE_DIM_MAX || h > MEDIA_IMAGE_DIM_MAX) return MEDIA_IMAGE_ERR_DIM;
    if (stride != w * bpp) return MEDIA_IMAGE_ERR_STRIDE;
    /* At most 1024 * 1024 * 3: no overflow. */
    const uint32_t pixels = (uint32_t)h * stride + alpha_plane * (uint32_t)h * w;
    if (size - MEDIA_IMAGE_HEADER_SIZE != pixels) return MEDIA_IMAGE_ERR_SIZE;

    if (out) {
        out->w = w;
        out->h = h;
        out->stride = stride;
        out->cf = cf;
        out->data_offset = MEDIA_IMAGE_HEADER_SIZE;
        out->data_size = pixels;
    }
    return MEDIA_IMAGE_OK;
}

const char *media_image_err_str(media_image_err_t e) {
    switch (e) {
        case MEDIA_IMAGE_OK: return "ok";
        case MEDIA_IMAGE_ERR_TRUNCATED: return "shorter than the header";
        case MEDIA_IMAGE_ERR_MAGIC: return "bad magic";
        case MEDIA_IMAGE_ERR_CF: return "colour format not A8, RGB565 or RGB565A8";
        case MEDIA_IMAGE_ERR_FLAGS: return "flags or reserved not 0";
        case MEDIA_IMAGE_ERR_DIM: return "width/height 0 or above 1024";
        case MEDIA_IMAGE_ERR_STRIDE: return "bad stride";
        case MEDIA_IMAGE_ERR_SIZE: return "size is not header + pixels";
    }
    return "?";
}

bool media_image_name_ok(const char *name) {
    if (!name) return false;
    size_t n = 0;
    for (; name[n]; n++) {
        const char c = name[n];
        if (n >= MEDIA_IMAGE_NAME_MAX) return false;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return n > 0;
}

bool media_image_icon_key(char *out, size_t cap, const char *name, uint16_t w, uint16_t h) {
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!media_image_name_ok(name)) return false;
    if (w == 0 || h == 0 || w > MEDIA_IMAGE_DIM_MAX || h > MEDIA_IMAGE_DIM_MAX) return false;
    const int n = snprintf(out, cap, "ico-%s-%ux%u.bin", name, (unsigned)w, (unsigned)h);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return false;
    }
    return true;
}
