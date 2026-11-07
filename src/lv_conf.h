#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16
/* LV_COLOR_16_SWAP is gone in LVGL 9: the displays are set to
 * LV_COLOR_FORMAT_RGB565_SWAPPED in LvglDisplay.cpp instead. */
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (61440U)    /* 64 KB */

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <stdint.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (esp_timer_get_time() / 1000)

/*====================
   FEATURE CONFIGURATION
 *====================*/
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4

/*-------------
 * GPU - Disable all
 *-----------*/
#define LV_USE_GPU_ARM2D 0
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_SWM341_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0

/*-------------
 * Logging
 *-----------*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/*-------------
 * Asserts
 *-----------*/
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0
#define LV_ASSERT_HANDLER while(1);

/*-------------
 * Others
 *-----------*/
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0
#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA 1
#define LV_ENABLE_GC 0
#define LV_BIG_ENDIAN_SYSTEM 0
#define LV_USE_LARGE_COORD 0

/*==================
 *   FONT USAGE - MINIMAL
 *=================*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

#define LV_FONT_MONTSERRAT_12_SUBPX 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK 0
#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0

#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_FONT_SUBPX 1
#define LV_USE_FONT_PLACEHOLDER 1

/*=================
 *  TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
 *  WIDGET USAGE - MINIMAL
 *================*/
#define LV_USE_ARC        1  /* Required by spinner */
#define LV_USE_BAR        1  /* For progress bar */
#define LV_USE_BTN        1  /* Required by some widgets */
#define LV_USE_BTNMATRIX  0
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   1  /* Required by calendar */
#define LV_USE_IMG        1  /* Required for SVG/image support */
#define LV_USE_LABEL      1  /* Essential */
#define LV_USE_LINE       1  /* Required by scale */
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   1  /* Required by keyboard and spinbox */
#define LV_USE_TABLE      0

/* Additional widgets that need to be explicitly disabled */
#define LV_USE_CALENDAR   0
#define LV_USE_KEYBOARD   0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0

/*==================
 * VECTOR GRAPHICS & SVG
 *==================*/
#define LV_USE_FLOAT 1              /* Required for matrix operations */
#define LV_USE_MATRIX 0
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_THORVG_EXTERNAL 0
#define LV_USE_SVG 0
#define LV_USE_SVG_ANIMATION 0
#define LV_USE_SVG_DEBUG 0

/*==================
 * OTHER LIBS
 *==================*/
#define LV_USE_FS 0
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 1
#define LV_USE_FREETYPE 0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0
#define LV_USE_TINY_TTF 0
#define LV_USE_BARCODE 0

/*==================
 * EXAMPLES & DEMOS - DISABLE ALL
 *==================*/
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif /*LV_CONF_H*/