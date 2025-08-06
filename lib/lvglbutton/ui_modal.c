/*#include "ui.h"

lv_obj_t* modal;
lv_obj_t* modallabel;
lv_style_t modalstyle;

void setmodal(lv_obj_t* screen, char* txt) {
	lv_style_init(&modalstyle);
	lv_style_set_bg_opa(&modalstyle, 255);
	lv_style_set_bg_color(&modalstyle, lv_color_hex(0x333333));
	lv_style_set_text_color(&modalstyle, lv_color_hex(0xFFFFFF));

	//lv_disp_set_default(disp);
	//lv_obj_t* toplayer = lv_layer_top();
	//modal = lv_obj_create(toplayer);
	modal = lv_obj_create(screen);
	lv_obj_move_foreground(modal);
	lv_obj_set_x(modal, lv_pct(10));
	lv_obj_set_y(modal, lv_pct(20));
	lv_obj_set_size(modal, lv_pct(80), lv_pct(60));
	lv_obj_add_style(modal, &modalstyle, 0);

	lv_obj_t* modallabel = lv_label_create(modal);
	lv_obj_set_align(modallabel, LV_ALIGN_CENTER);
	lv_label_set_long_mode(modallabel, LV_LABEL_LONG_WRAP);
	lv_obj_set_flex_grow(modallabel, 1);
	lv_label_set_text(modallabel, txt);
	lv_obj_center(modallabel);
}
*/