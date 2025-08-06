#include "ui.h"
#include "ui_text.h"

typedef struct {
    /* The main container */
	lv_obj_t* main;
	lv_obj_t* title;
	lv_obj_t* text;
	lv_obj_t* hint;

    /* The base font size for text */
    uint8_t base_font_size;
} box_dsc_t;


static lv_style_t style_box;
static lv_style_t style_title;
static lv_style_t style_text;
static lv_style_t style_hint;
static lv_style_t style_underline;

extern lv_font_t* fnt_semibold_14;
extern lv_font_t* fnt_medium_14;
extern lv_font_t* fnt_medium_32;
extern lv_font_t* fnt_medium_48;
extern lv_font_t* fnt_medium_72;

void ui_box_create_styles(void)
{
	lv_style_init(&style_box);
	lv_style_set_bg_opa(&style_box, 0);
	lv_style_set_layout(&style_box, LV_LAYOUT_FLEX);
	lv_style_set_flex_flow(&style_box, LV_FLEX_FLOW_ROW);
	lv_style_set_flex_cross_place(&style_box, LV_FLEX_ALIGN_END);
	lv_style_set_width(&style_box, 150);
	lv_style_set_height(&style_box, LV_SIZE_CONTENT);
    lv_style_set_border_width(&style_box, 0);
    lv_style_set_outline_width(&style_box, 0);

    // Set the radius to 0 to ensure straight corners
    lv_style_set_radius(&style_box, 0);

	lv_style_init(&style_underline);
	lv_style_set_border_color(&style_underline, lv_color_hex3(0x777));
	lv_style_set_border_width(&style_underline, 2);
	lv_style_set_border_side(&style_underline, LV_BORDER_SIDE_BOTTOM);

	lv_style_set_pad_row(&style_box, 0); /* between label and text */
	lv_style_set_pad_bottom(&style_box, 0);
    lv_style_set_pad_all(&style_box, 0); /* between label and text */

	lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, fnt_semibold_14);

	lv_style_init(&style_text);
	lv_style_set_text_color(&style_text, lv_color_hex(0xffffff));

	lv_style_init(&style_hint);
	lv_style_set_text_color(&style_hint, lv_color_hex3(0x999));
	lv_style_set_text_font(&style_hint, fnt_medium_14);
}

void ui_box_set_default_title_color(long color)
{
	lv_style_set_text_color(&style_title, lv_color_hex(color));
}

lv_obj_t* ui_create_text_container(lv_obj_t *parent)
{
    lv_obj_t* txt = lv_obj_create(parent);
    lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(txt, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(txt, 0, 0);
    lv_obj_set_style_border_width(txt, 0, 0);
    lv_obj_set_style_pad_all(txt, lv_dpx(0), 0);

    //lv_obj_add_flag(txt, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    //lv_obj_set_flex_grow(txt, 1);

    return txt;
}
/**
 * Create a box with a title, text and hint
 */
lv_obj_t* ui_box_create()
{
	lv_obj_t* box = lv_obj_create(content);
	lv_obj_add_style(box, &style_box, 0);

	box_dsc_t* b = lv_malloc(sizeof(box_dsc_t));

    /* Set the base font size so that the text can be scaled later relatively */
    b->base_font_size = 1;
	lv_obj_set_user_data(box, b);

	b->main = box;

    /* create the title */
	b->title = ui_create_text_container(box);
    lv_obj_add_style(b->title, &style_title, 0);

    /* create the text container */
    b->text = ui_create_text_container(box);
    lv_obj_add_flag(b->text, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_obj_set_flex_grow(b->text, 1);

    /* create the hint container */
	b->hint = ui_create_text_container(box);
	lv_obj_add_style(b->hint, &style_hint, 0);

	return box;
}

void ui_box_update(lv_obj_t* box, const char* caption, const char* text, const char* unit, int x, int y, int font, int width, int align, int boxtype) {
	if (boxtype == 0)
		lv_obj_add_style(box, &style_underline, 0);
	if (boxtype == 1)
		lv_obj_remove_style(box, &style_underline, 0);

	ui_box_set_caption(box, caption);
	ui_box_set_text(box, text);
	ui_box_set_hint(box, unit);
    ui_box_set_font_size(box, font, boxtype);
	lv_obj_set_align(box, align == 0 ? LV_ALIGN_TOP_LEFT : align);

	lv_obj_set_pos(box, x * 320 / 100, y * 240 / 100);
	lv_obj_set_width(box, width * 320 / 100);
}


void ui_box_set_caption(lv_obj_t* box, const char* caption)
{
	box_dsc_t* b = lv_obj_get_user_data(box);
    ui_render_text(b->title, caption, &style_title, b->base_font_size, true);
}

void ui_box_set_text(lv_obj_t* box, const char* text)
{
	box_dsc_t* b = lv_obj_get_user_data(box);

    ui_render_text(b->text, text, &style_text, b->base_font_size, false);
}

void ui_box_set_hint(lv_obj_t* box, const char* hint)
{
	box_dsc_t* b = lv_obj_get_user_data(box);
    ui_render_text(b->hint, hint, &style_hint, b->base_font_size, false);
}

void ui_box_set_font_size(lv_obj_t* box, uint8_t size, int boxtype)
{
	box_dsc_t * b = lv_obj_get_user_data(box);

    b->base_font_size = size;

	switch (size) {
	case 1:
		lv_obj_set_style_pad_bottom(b->title, 2, 0);
		lv_obj_set_style_pad_bottom(b->main, 1, 0);
		break;
	case 2:
		lv_obj_set_style_pad_bottom(b->title, 4, 0);
		lv_obj_set_style_pad_bottom(b->main, 3, 0);
		break;
	case 3:
		lv_obj_set_style_pad_bottom(b->title, boxtype ? 0 : 4, 0);
		lv_obj_set_style_pad_bottom(b->main, 8, 0);
		break;
	case 4:
		lv_obj_set_style_pad_bottom(b->title, boxtype ? 6 : 10, 0);
		//lv_obj_set_style_text_font(b->text, fnt_medium_48, 0);
		//lv_label_set_long_mode(b->text, LV_LABEL_LONG_CLIP);
		lv_obj_set_style_pad_bottom(b->main, 8, 0);
		break;
	case 5:
		lv_obj_set_style_pad_bottom(b->title, boxtype ? 6 : 14, 0);
		//lv_obj_set_style_text_font(b->text, fnt_medium_72, 0);
		//lv_label_set_long_mode(b->text, LV_LABEL_LONG_CLIP);
		lv_obj_set_style_pad_bottom(b->main, 12, 0);
		break;
	}
}
