#include "ui.h"

static lv_style_t style_screen;
static lv_obj_t* screen;
static lv_obj_t* header;
static lv_obj_t* img_ip;
static lv_obj_t* label_ip;
static lv_obj_t* img_wifi;
static lv_obj_t* img_connect;

lv_obj_t* content;

int statusbarheight = 18;

void ui_main_statusbarobj(int statusbar) {
	//statusbar 0: off, 1: top, 2: bottom
	lv_obj_clear_flag(header, LV_OBJ_FLAG_HIDDEN);
	if (!statusbar)
		lv_obj_add_flag(header, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_set_pos(header, 0, (statusbar == 2) ? (240 - statusbarheight) : 0);
}

void ui_main_init(lv_obj_t* scr)
{
	screen = scr;
	//screen == 320x240 display
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(0x000000));
    lv_style_set_bg_opa(&style_screen, LV_OPA_100);
	lv_obj_add_style(screen, &style_screen, 0);

    lv_obj_add_style(screen, &htmldiv, 0);
	//lv_obj_set_scrollbar_mode(main, LV_SCROLLBAR_MODE_OFF);	//Not necessery because header and cont have fixed size

	//CONTENT area
	content = lv_obj_create(screen);
	lv_obj_add_style(content, &htmldiv, 0);
	lv_obj_set_size(content, lv_pct(100), 240 /*- (statusbar ? statusbarheight : 0)*/);
	lv_obj_set_pos(content, 0, /*(statusbar && statusbartop) ? statusbarheight :*/ 0);
	lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);


	//Header will always be created, if statusbar == 0 it will be hidden
	header = lv_obj_create(screen);
	lv_obj_add_style(header, &htmldiv, 0);
	lv_obj_set_style_bg_color(header, lv_color_hex(0x707070), 0);
	lv_obj_set_style_bg_opa(header, LV_OPA_50, 0);

	//lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_size(header, lv_pct(100), 20);
	lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(header, 10, 0);
	lv_obj_set_style_pad_hor(header, 4, 0);
	lv_obj_set_style_bg_opa(header, 0, 0);

	//Header Wifi icon
	img_ip = lv_img_create(header);
	//LV_IMG_DECLARE(icon_wifi);
	//lv_img_set_src(img_ip, &icon_wifi);

	//Header IP address/status
	label_ip = lv_label_create(header);
	lv_label_set_text(label_ip, "");
	lv_obj_set_flex_grow(label_ip, 1);
	lv_obj_set_style_text_color(label_ip, lv_color_hex3(0x999), 0);
}



void ui_set_ip_address(const char* ip_txt)
{
    if (label_ip)
		lv_label_set_text(label_ip, ip_txt);
}

void ui_set_wifi_state(bool en)
{
    lv_obj_set_style_bg_img_recolor_opa(img_wifi, en ? 255 : 0, 0);
}

void ui_set_connect_state(bool en)
{
    lv_obj_set_style_bg_img_recolor_opa(img_connect, en ? 255 : 0, 0);
}


