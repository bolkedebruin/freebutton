// Emulator shim: the slice of LVGL the firmware's components use directly.
//
// LVGL itself is not in the emulator (stage 4). The components
// (src/components) are the firmware's; below them, where ui_box.c,
// ui_button.c, ui_clock.c and LVGL's roller and image widgets would draw,
// tools/js/emulator/ui_shim.cpp keeps what was asked for (texts as resolved
// runs, fonts, colours, positions) and the host draws it. An lv_obj_t is one
// such virtual widget; the functions here set its fields.
#pragma once
#include "esp_log.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lv_obj_t lv_obj_t;
typedef struct lv_display_t lv_display_t;
typedef int32_t lv_coord_t;
typedef struct { int32_t x; int32_t y; } lv_point_precise_t;

/* A font instance: a face of the font table (fonts.h) at a pixel size. */
typedef struct lv_font_t {
    int16_t face;
    int16_t px;
    int32_t line_height;
} lv_font_t;

/* A shared style of ui_style.h: the style element it belongs to. */
typedef struct lv_style_t {
    int element;
} lv_style_t;

/* A raster of the images bucket (ui/MediaImage.h): its key. */
typedef struct lv_image_dsc_t {
    char key[112];
    uint16_t refs;
} lv_image_dsc_t;
typedef lv_image_dsc_t lv_image_t;

typedef struct { uint8_t blue; uint8_t green; uint8_t red; } lv_color_t;
static inline lv_color_t lv_color_hex(const uint32_t c) {
    lv_color_t color = {(uint8_t)(c & 0xff), (uint8_t)((c >> 8) & 0xff), (uint8_t)((c >> 16) & 0xff)};
    return color;
}
static inline uint32_t lv_color_to_u32(const lv_color_t c) {
    return ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | c.blue;
}

typedef int lv_align_t;
enum { LV_ALIGN_DEFAULT = 0, LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_MID, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_LEFT,
       LV_ALIGN_BOTTOM_MID, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_LEFT_MID, LV_ALIGN_RIGHT_MID, LV_ALIGN_CENTER };

typedef uint32_t lv_part_t;
#define LV_PART_MAIN 0x000000u
#define LV_PART_SELECTED 0x040000u

typedef int lv_style_prop_t;
enum { LV_STYLE_TEXT_FONT = 1, LV_STYLE_TEXT_LINE_SPACE, LV_STYLE_TEXT_COLOR };

typedef int lv_anim_enable_t;
enum { LV_ANIM_OFF = 0, LV_ANIM_ON = 1 };
typedef int lv_roller_mode_t;
enum { LV_ROLLER_MODE_NORMAL = 0 };
#define LV_OBJ_FLAG_CLICKABLE 0x0002u

/* The pool: nothing is drawn here, so there is always room (LvglRoom.h). */
typedef struct {
    size_t total_size;
    size_t free_size;
    size_t free_biggest_size;
} lv_mem_monitor_t;
void lv_mem_monitor(lv_mem_monitor_t* mon);

lv_display_t* lv_display_get_default(void);
void lv_display_set_default(lv_display_t* display);
lv_obj_t* lv_display_get_screen_active(lv_display_t* display);
lv_obj_t* lv_scr_act(void);

void lv_obj_delete(lv_obj_t* obj);
static inline void lv_obj_del(lv_obj_t* obj) { lv_obj_delete(obj); }
void lv_obj_invalidate(lv_obj_t* obj);
void lv_obj_set_pos(lv_obj_t* obj, int32_t x, int32_t y);
void lv_obj_set_width(lv_obj_t* obj, int32_t width);
void lv_obj_set_hidden(lv_obj_t* obj, bool hidden);
void lv_obj_set_clickable(lv_obj_t* obj, bool clickable);
void lv_obj_remove_flag(lv_obj_t* obj, uint32_t flag);
void lv_obj_remove_style_all(lv_obj_t* obj);
void lv_obj_add_style(lv_obj_t* obj, const lv_style_t* style, lv_part_t part);
bool lv_obj_remove_local_style_prop(lv_obj_t* obj, lv_style_prop_t prop, lv_part_t part);
void lv_obj_set_style_text_font(lv_obj_t* obj, const lv_font_t* font, lv_part_t part);
const lv_font_t* lv_obj_get_style_text_font(lv_obj_t* obj, lv_part_t part);
void lv_obj_set_style_text_line_space(lv_obj_t* obj, int32_t space, lv_part_t part);
void lv_obj_set_style_text_color(lv_obj_t* obj, lv_color_t color, lv_part_t part);
void lv_obj_set_style_bg_color(lv_obj_t* obj, lv_color_t color, lv_part_t part);
void lv_obj_set_style_radius(lv_obj_t* obj, int32_t radius, lv_part_t part);
void lv_obj_set_style_pad_hor(lv_obj_t* obj, int32_t pad, lv_part_t part);
void lv_obj_set_style_anim_duration(lv_obj_t* obj, uint32_t ms, lv_part_t part);

lv_obj_t* lv_roller_create(lv_obj_t* parent);
void lv_roller_set_options(lv_obj_t* roller, const char* options, lv_roller_mode_t mode);
void lv_roller_set_selected(lv_obj_t* roller, uint32_t selected, lv_anim_enable_t anim);
void lv_roller_set_visible_row_count(lv_obj_t* roller, uint32_t rows);

lv_obj_t* lv_image_create(lv_obj_t* parent);

#ifdef __cplusplus
}
#endif
