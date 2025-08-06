#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif


#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//extern int accentcolor;

extern lv_style_t htmldiv;
extern lv_style_t modalstyle;

extern lv_obj_t * content;

void ui_styles();

void ui_main_init(lv_obj_t * screen);
void ui_main_statusbarobj(int statusbar);

void ui_set_ip_address(const char * ip_txt);
void ui_set_wifi_state(bool en);
void ui_set_connect_state(bool en);

void ui_box_create_styles(void);
void ui_box_set_default_title_color(long color);

lv_obj_t * ui_box_create();
void ui_box_update(lv_obj_t* box, const char* caption, const char* text, const char* unit, int x, int y, int font, int width, int align, int boxtype);


void ui_box_set_caption(lv_obj_t * box, const char * caption);
void ui_box_set_text(lv_obj_t * box, const char * text);
void ui_box_set_hint(lv_obj_t * box, const char * hint);
void ui_box_set_font_size(lv_obj_t * box, uint8_t size, int boxtype);

lv_obj_t * ui_button_create();
void ui_button_delete(lv_obj_t * button);

void ui_screen_init(void);

void ui_button_styles(void);
void ui_button_set_default_title_color(long color);

void ui_button_set_title(lv_obj_t * button, const char * title);
void ui_button_set_text(lv_obj_t * button, const char * text);

void ui_setmodal(lv_obj_t* screen, char* txt);

#ifdef __cplusplus
}
#endif


