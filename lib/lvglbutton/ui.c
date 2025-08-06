#include "ui.h"

//extern int accentcolor;

lv_style_t htmldiv;
lv_style_t modalstyle;

void ui_styles()
{
	lv_style_init(&htmldiv);
	lv_style_set_pad_left(&htmldiv, 0);
	lv_style_set_pad_right(&htmldiv, 0);
	lv_style_set_pad_top(&htmldiv, 0);
	lv_style_set_pad_bottom(&htmldiv, 0);
	lv_style_set_radius(&htmldiv, 0);
	//lv_style_set_bg_opa(&htmldiv, 0);
	lv_style_set_text_color(&htmldiv, lv_color_hex(0xFFFFFF));
	lv_style_set_bg_color(&htmldiv, lv_color_hex(0x000000));
	lv_style_set_border_width(&htmldiv, 0);
}

