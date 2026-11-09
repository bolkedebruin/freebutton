
#ifndef FONTS_H
#define FONTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* The UI fonts. init_fonts() points them at the streaming .bin fonts on
 * LittleFS (S:/fonts/), or at the compiled-in copy when a file is missing or
 * invalid. destroy_fonts() points them back at the compiled-in copies. */
extern lv_font_t *fnt_semibold_14;
extern lv_font_t *fnt_semibold_18;

extern lv_font_t *fnt_medium_14;
extern lv_font_t *fnt_medium_24;
extern lv_font_t *fnt_medium_32;
extern lv_font_t *fnt_medium_48;
extern lv_font_t *fnt_medium_72;

/* Initialize all fonts */
void init_fonts(void);

/* Destroy all fonts */
void destroy_fonts(void);

#ifdef __cplusplus
}
#endif

#endif
