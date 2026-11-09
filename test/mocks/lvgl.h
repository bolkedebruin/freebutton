//
// Minimal host stand-in for lvgl.h, for native tests only (test/mocks is on
// the native include path). It declares just the fmt_txt font types and macros
// that the generated src/ui/fonts/mnt_*.c files use, so a test can #include
// those files as plain data. Field order matches the generated designated
// initializers (C++ requires declaration order); widths are generous.
//
#pragma once

#include <stddef.h>
#include <stdint.h>

#define LVGL_VERSION_MAJOR 9
#define LVGL_VERSION_MINOR 6
#define LVGL_VERSION_PATCH 0
#define LV_VERSION_CHECK(x, y, z) \
    ((x) == LVGL_VERSION_MAJOR && ((y) < LVGL_VERSION_MINOR || ((y) == LVGL_VERSION_MINOR && (z) <= LVGL_VERSION_PATCH)))

#define LV_ATTRIBUTE_LARGE_CONST
#define LV_FONT_SUBPX_NONE 0

typedef enum {
    LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
    LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
    LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
} lv_font_fmt_txt_cmap_type_t;

typedef struct {
    uint32_t bitmap_index;
    uint32_t adv_w;
    uint32_t box_w;
    uint32_t box_h;
    int32_t ofs_x;
    int32_t ofs_y;
} lv_font_fmt_txt_glyph_dsc_t;

typedef struct {
    uint32_t range_start;
    uint32_t range_length;
    uint32_t glyph_id_start;
    const uint16_t *unicode_list;
    const void *glyph_id_ofs_list;
    uint32_t list_length;
    lv_font_fmt_txt_cmap_type_t type;
} lv_font_fmt_txt_cmap_t;

typedef struct {
    const int8_t *class_pair_values;
    const uint8_t *left_class_mapping;
    const uint8_t *right_class_mapping;
    uint32_t left_class_cnt;
    uint32_t right_class_cnt;
} lv_font_fmt_txt_kern_classes_t;

typedef struct {
    const void *glyph_ids;
    const int8_t *values;
    uint32_t pair_cnt;
    uint32_t glyph_ids_size; /* 0: uint8 ids, 1: uint16 ids */
} lv_font_fmt_txt_kern_pair_t;

typedef struct {
    const uint8_t *glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t *glyph_dsc;
    const lv_font_fmt_txt_cmap_t *cmaps;
    const void *kern_dsc;
    uint32_t kern_scale;
    uint32_t cmap_num;
    uint32_t bpp;
    uint32_t kern_classes;
    uint32_t bitmap_format; /* 0 plain, 1 compressed + prefilter, 2 compressed */
} lv_font_fmt_txt_dsc_t;

typedef struct lv_font_t {
    const void *get_glyph_dsc;
    const void *get_glyph_bitmap;
    int32_t line_height;
    int32_t base_line;
    int32_t subpx;
    int32_t underline_position;
    int32_t underline_thickness;
    const void *dsc;
    const void *fallback;
    void *user_data;
} lv_font_t;

#define lv_font_get_glyph_dsc_fmt_txt ((const void *)0)
#define lv_font_get_bitmap_fmt_txt ((const void *)0)
