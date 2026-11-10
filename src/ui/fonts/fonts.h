
#ifndef FONTS_H
#define FONTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* The UI fonts. init_fonts() points them at TrueType fonts rendered by LVGL
 * tiny_ttf from the media store (bucket `fonts`: medium.ttf, semibold.ttf,
 * icons.ttf; see fonts.c), or at LVGL's built-in 14 px Montserrat when a file
 * is missing or invalid. The pointers never change after init_fonts(): a
 * reload replaces what they point at. Call init_fonts() and destroy_fonts()
 * with the LVGL lock held (or before the LVGL task runs). */
extern lv_font_t *fnt_semibold_14;
extern lv_font_t *fnt_semibold_18;

extern lv_font_t *fnt_medium_14;
extern lv_font_t *fnt_medium_24;
extern lv_font_t *fnt_medium_32;
extern lv_font_t *fnt_medium_48;
extern lv_font_t *fnt_medium_72;

/* Initialize all fonts */
void init_fonts(void);

/* Destroy all fonts (fnt_* then hold the built-in font) and release the files */
void destroy_fonts(void);

/* A file in bucket `fonts` changed (uploaded, replaced, deleted). Any task;
 * only records the request. The LVGL task reloads all fonts from the bucket
 * 1.5 s after the last request, so a batch of uploads reloads once. */
void fonts_request_reload(void);

#ifdef __cplusplus
}
#endif

#endif
