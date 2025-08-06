#include <stdio.h>

#include "ui.h"

typedef struct {
	lv_obj_t * main;
	lv_obj_t * title;
	lv_obj_t * text;
    lv_obj_t * icon;
} button_dsc_t;


//static lv_style_t style_base;
static lv_style_t style_mini;
static lv_style_t style_mini_top;
static lv_style_t style_mini_bottom;
static lv_style_t style_icon;

extern lv_font_t *fnt_semibold_18;
extern lv_font_t * fnt_medium_24;

void ui_button_styles(void)
{
    //lv_style_init(&style_base);
  
	lv_style_init(&style_mini);
	lv_style_set_bg_opa(&style_mini, 0);
	lv_style_set_layout(&style_mini, LV_LAYOUT_FLEX);
	lv_style_set_flex_flow(&style_mini, LV_FLEX_FLOW_COLUMN);
	//lv_style_set_flex_align(&style_mini, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_style_set_flex_cross_place(&style_mini, LV_FLEX_ALIGN_CENTER);
	lv_style_set_flex_main_place(&style_mini, LV_FLEX_ALIGN_CENTER);

	lv_style_set_pad_row(&style_mini, 8);

	lv_style_init(&style_mini_top);

	//LV_FONT_DECLARE(mnt_medium_32);
	lv_style_set_text_font(&style_mini_top, fnt_semibold_18);

	lv_style_init(&style_mini_bottom);
	lv_style_set_text_font(&style_mini_bottom, fnt_medium_24);

	/*lv_style_init(&style_icon);
	LV_FONT_DECLARE(fa_regular_30);
	lv_style_set_text_font(&style_icon, &fa_regular_30);*/
}

void ui_button_set_default_title_color(long color)
{
	lv_style_set_text_color(&style_mini_top, lv_color_hex(color));
}
 
lv_obj_t * ui_button_create() {
	// content area style
	static lv_style_t button;
	lv_style_init(&button);
	lv_style_set_flex_flow(&button, LV_FLEX_FLOW_COLUMN);
    lv_style_set_flex_main_place(&button, LV_FLEX_ALIGN_START);
	lv_style_set_flex_cross_place(&button, LV_FLEX_ALIGN_CENTER);
	lv_style_set_layout(&button, LV_LAYOUT_FLEX);
	//lv_style_set_pad_column(&content, 0);
    lv_style_set_pad_row(&button, 0);
    lv_style_set_bg_color(&button, lv_color_hex(0x000000));
    lv_style_set_bg_opa(&button, LV_OPA_100);
	//lv_style_set_border_width(&button, 1);

	// CONTENT
	lv_obj_t * cont = lv_obj_create(lv_scr_act());

	button_dsc_t * b = lv_malloc(sizeof(button_dsc_t));
	lv_obj_set_user_data(cont, b);
	b->main = cont;

	lv_obj_set_size(cont, 160, 80);
	lv_obj_center(cont);
	lv_obj_add_style(cont, &htmldiv, 0);
	lv_obj_add_style(cont, &button, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

	// CONTENT => TOP
	lv_obj_t * topobj = lv_obj_create(cont);
	lv_obj_set_size(topobj, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_add_style(topobj, &htmldiv, 0);
	lv_obj_set_scrollbar_mode(topobj, LV_SCROLLBAR_MODE_OFF);

	// CONTENT => TOP => LABEL
	lv_obj_t * toplabel = lv_label_create(topobj);
	b->title = toplabel;
	//lv_style_set_border_width(&style_mini_top, 2);
	//lv_style_set_border_color(&style_mini_top, lv_palette_main(LV_PALETTE_BLUE));
	lv_obj_add_style(toplabel, &style_mini_top, 0);
	//lv_label_set_text_fmt(topLabel, "Light scene");
	lv_obj_center(toplabel);
	//lv_obj_set_width(label, LV_LABEL_LONG_WRAP);

	// CONTENT => BOTTOM
	lv_obj_t * botobj = lv_obj_create(cont);
	lv_obj_set_size(botobj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(botobj, 1);
	lv_obj_add_style(botobj, &htmldiv, 0);
	lv_obj_set_scrollbar_mode(botobj, LV_SCROLLBAR_MODE_OFF);

	// CONTENT => BOTTOM => LABEL
	lv_obj_t * botlabel = lv_label_create(botobj);
	b->text = botlabel;
    lv_label_set_long_mode(botlabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(botlabel, lv_pct(100));
    lv_label_set_text(botlabel, ""); //Remove default text
	lv_obj_add_style(botlabel, &style_mini_bottom, 0);
    lv_obj_set_style_text_align(botlabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(botlabel);


    // CONTENT => BOTTOM => ICON
	/*lv_obj_t* img = lv_img_create(botobj);
	LV_IMG_DECLARE(volplus);
   	lv_img_set_src(img, &volplus);
	lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
	b->icon = img;*/

    return cont;
}


void ui_button_set_title(lv_obj_t * button, const char * title)
{
	const button_dsc_t * b = lv_obj_get_user_data(button);
	if (b == NULL) {
		printf("ui_button_set_title: button_dsc_t is NULL\n");
		return;
	}

	lv_label_set_text(b->title, title);
	//If no title provided, set height to 0, this will center remaining text
	if (strlen(title) == 0)
		lv_obj_set_size(b->title, lv_pct(100), 0);
    else
		lv_obj_set_size(b->title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
}

void ui_button_set_text(lv_obj_t * button, const char * text)
{
	const button_dsc_t * b = lv_obj_get_user_data(button);
	if (b == NULL) {
		printf("ui_button_set_text: button_dsc_t is NULL\n");
		return;
	}
	lv_label_set_text(b->text, text);
}

void ui_button_set_icon(lv_obj_t* button, const char* icon)
{
    button_dsc_t* b = lv_obj_get_user_data(button);
    //lv_label_set_text(b->icon, icon);
}
