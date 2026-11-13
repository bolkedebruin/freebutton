#include <stdio.h>
#include <string.h>
#include "ui_button.h"
#include "ui.h"
#include "lvgl.h"
#include "ui_style.h"


static lv_style_t style_container;
static lv_style_t style_mini;
static lv_style_t style_icon;

/* LVGL lock held. The labels' text styles are ui_style.h's (key.label,
 * key.toplabel). */
void ui_button_styles(void)
{
    // Initialize container style for clean layout
    lv_style_init(&style_container);
    lv_style_set_bg_opa(&style_container, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_container, 0);
    lv_style_set_pad_all(&style_container, 0);
    lv_style_set_layout(&style_container, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&style_container, LV_FLEX_FLOW_COLUMN);
    lv_style_set_flex_cross_place(&style_container, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_main_place(&style_container, LV_FLEX_ALIGN_CENTER);
    /* The column's single track centred too: cross_place only centres items
     * within the track, which is as wide as its widest visible child. The
     * labels are 100% wide; the bottom area's icon is not. */
    lv_style_set_flex_track_place(&style_container, LV_FLEX_ALIGN_CENTER);

	lv_style_init(&style_mini);
	lv_style_set_bg_opa(&style_mini, 0);
	lv_style_set_layout(&style_mini, LV_LAYOUT_FLEX);
	lv_style_set_flex_flow(&style_mini, LV_FLEX_FLOW_COLUMN);
	lv_style_set_flex_cross_place(&style_mini, LV_FLEX_ALIGN_CENTER);
	lv_style_set_flex_main_place(&style_mini, LV_FLEX_ALIGN_CENTER);
	lv_style_set_pad_row(&style_mini, 8);

	lv_style_init(&style_icon);
}
 
lv_obj_t *ui_button_create(lv_obj_t* parent) {
    static bool button_style_initialized = false;
    static lv_style_t button_style;

    if (!button_style_initialized) {
        lv_style_init(&button_style);
        lv_style_set_flex_flow(&button_style, LV_FLEX_FLOW_COLUMN);
        lv_style_set_flex_main_place(&button_style, LV_FLEX_ALIGN_START);
        lv_style_set_flex_cross_place(&button_style, LV_FLEX_ALIGN_CENTER);
        lv_style_set_layout(&button_style, LV_LAYOUT_FLEX);
        lv_style_set_pad_row(&button_style, 0);
        lv_style_set_bg_color(&button_style, lv_color_hex(0x000000));
        lv_style_set_bg_opa(&button_style, LV_OPA_100);
        // Remove rounded corners
        lv_style_set_radius(&button_style, 0);
        lv_style_set_border_width(&button_style, 0);
        button_style_initialized = true;
    }

    lv_obj_t *cont = lv_obj_create(parent);
    if (!cont) {
        return NULL;
    }

    button_dsc_t *b = (button_dsc_t *)lv_malloc(sizeof(button_dsc_t));
    if (!b) {
        lv_obj_del(cont);
        return NULL;
    }

    lv_obj_set_user_data(cont, b);
    b->main = cont;

	lv_obj_set_size(cont, 160, 80);
	lv_obj_center(cont);
	lv_obj_add_style(cont, &style_container, 0);
	lv_obj_add_style(cont, &button_style, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

	// CONTENT => TOP
	lv_obj_t * topobj = lv_obj_create(cont);
	lv_obj_set_size(topobj, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_add_style(topobj, &style_container, 0);
	lv_obj_set_scrollbar_mode(topobj, LV_SCROLLBAR_MODE_OFF);

	// CONTENT => TOP => LABEL
	lv_obj_t * toplabel = lv_label_create(topobj);
	b->title = toplabel;
	lv_obj_add_style(toplabel, ui_style_get(STYLE_EL_KEY_TOPLABEL), 0); /* centred unless "style" aligns it */
	lv_obj_set_width(toplabel, lv_pct(100));
	lv_obj_align(toplabel, LV_ALIGN_CENTER, 0, 0);

	// CONTENT => BOTTOM
	lv_obj_t * botobj = lv_obj_create(cont);
	lv_obj_set_size(botobj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(botobj, 1);
	lv_obj_add_style(botobj, &style_container, 0);
	lv_obj_set_scrollbar_mode(botobj, LV_SCROLLBAR_MODE_OFF);

	// CONTENT => BOTTOM => LABEL
	lv_obj_t * botlabel = lv_label_create(botobj);
	b->text = botlabel;
    lv_label_set_long_mode(botlabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(botlabel, lv_pct(100));
    lv_label_set_text(botlabel, ""); //Remove default text
	lv_obj_add_style(botlabel, ui_style_get(STYLE_EL_KEY_LABEL), 0);
    lv_obj_align(botlabel, LV_ALIGN_CENTER, 0, 0);


    // CONTENT => BOTTOM => ICON (initially hidden)
    lv_obj_t* icon = lv_image_create(botobj);
    b->icon = icon;
    lv_obj_set_hidden(icon, true);  // Hidden by default
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);

    return cont;
}


void ui_button_set_title(lv_obj_t * button, const char * title)
{
	const button_dsc_t * b = (const button_dsc_t *)lv_obj_get_user_data(button);
	if (b == NULL) {
		printf("ui_button_set_title: button_dsc_t is NULL\n");
		return;
	}

	lv_label_set_text(b->title, title);
	//If no title provided, set height to 0, this will center remaining text
	if (strlen(title) == 0)
		lv_obj_set_size(b->title, lv_pct(100), 0);
    else
		lv_obj_set_size(b->title, lv_pct(100), LV_SIZE_CONTENT);
}

void ui_button_set_text(lv_obj_t * button, const char * text)
{
	const button_dsc_t * b = (const button_dsc_t *)lv_obj_get_user_data(button);
	if (b == NULL) {
		printf("ui_button_set_text: button_dsc_t is NULL\n");
		return;
	}
	lv_label_set_text(b->text, text);
}

static void set_label_font(lv_obj_t* label, const lv_font_t* font)
{
    if (font != NULL) {
        lv_obj_set_style_text_font(label, font, 0);
    } else {
        lv_obj_remove_local_style_prop(label, LV_STYLE_TEXT_FONT, 0);
    }
}

void ui_button_set_fonts(lv_obj_t* button, const lv_font_t* text_font, const lv_font_t* title_font)
{
    const button_dsc_t* b = (const button_dsc_t*)lv_obj_get_user_data(button);
    if (b == NULL) {
        return;
    }
    set_label_font(b->text, text_font);
    set_label_font(b->title, title_font);
}

void ui_button_set_icon(lv_obj_t* button, const lv_image_dsc_t* dsc)
{
    const button_dsc_t* b = (const button_dsc_t*)lv_obj_get_user_data(button);
    if (!b || !b->icon) {
        printf("ui_button_set_icon: invalid button or icon object\n");
        return;
    }
    ui_image_set_raster(b->icon, dsc, lv_obj_get_style_text_color(b->text, 0));
    lv_obj_set_hidden(b->icon, dsc == NULL);
    lv_obj_set_hidden(b->text, dsc != NULL);
}

void ui_button_delete(lv_obj_t* button)
{
    if (button == NULL) {
        return;
    }
    button_dsc_t* b = (button_dsc_t*)lv_obj_get_user_data(button);
    lv_obj_set_user_data(button, NULL);
    lv_obj_delete(button);
    lv_free(b);
}
