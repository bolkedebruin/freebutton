#include "ui.h"
#include "ui_text.h"
#include "ui_style.h"
#include "style_logic.h"
#include "box_logic.h"
#include "fonts/fonts.h"

#include <stdlib.h>

typedef struct {
    /* The main container */
	lv_obj_t* main;
	lv_obj_t* title;
	lv_obj_t* text;
	lv_obj_t* hint;
	/* The value's icon; NULL while the item has none (created on demand) */
	lv_obj_t* icon;

    /* The fonts of value + unit and of the caption (ui_box_fonts_t) */
    ui_box_fonts_t fonts;
} box_dsc_t;


static lv_style_t style_box;
static lv_style_t style_underline;

/* LVGL lock held. The text styles of caption, value and unit are
 * ui_style.h's (display.caption, display.value, display.unit). */
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

}

lv_obj_t* ui_create_text_container(lv_obj_t *parent)
{
    lv_obj_t* txt = lv_obj_create(parent);
    lv_obj_set_scrollable(txt, false);  /* see ui_box_create() */
    lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(txt, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(txt, 0, 0);
    lv_obj_set_style_border_width(txt, 0, 0);
    lv_obj_set_style_pad_all(txt, lv_dpx(0), 0);

    //lv_obj_set_flex_in_new_track(txt, true);
    //lv_obj_set_flex_grow(txt, 1);

    return txt;
}
/**
 * Create a box with a title, text and hint
 */
lv_obj_t* ui_box_create(lv_obj_t* parent)
{
	lv_obj_t* box = lv_obj_create(parent);
	lv_obj_add_style(box, &style_box, 0);
	/* Safety net only: content is sized to fit (empty caption / unit are
	 * hidden). Should text still be wider than the box, it is clipped
	 * instead of making the box scrollable, which drew a scrollbar line. */
	lv_obj_set_scrollable(box, false);

	box_dsc_t* b = lv_malloc(sizeof(box_dsc_t));
	if (b == NULL) {
		lv_obj_delete(box);
		return NULL;
	}

    /* The fonts before ui_box_set_fonts(): the style's */
    b->fonts.face = FONT_FACE_NONE;
    b->fonts.fontsize = 0;
    b->fonts.caption_face = FONT_FACE_NONE;
    b->fonts.caption_fontsize = 0;
	lv_obj_set_user_data(box, b);

	b->main = box;
	b->icon = NULL;

    /* create the title */
	b->title = ui_create_text_container(box);
    lv_obj_add_style(b->title, ui_style_get(STYLE_EL_DISPLAY_CAPTION), 0);

    /* create the text container */
    b->text = ui_create_text_container(box);
    lv_obj_set_flex_in_new_track(b->text, true);
    lv_obj_set_flex_grow(b->text, 1);
	lv_obj_add_style(b->text, ui_style_get(STYLE_EL_DISPLAY_VALUE), 0);

    /* create the hint container */
	b->hint = ui_create_text_container(box);
	lv_obj_add_style(b->hint, ui_style_get(STYLE_EL_DISPLAY_UNIT), 0);

	return box;
}

/**
 * Delete a box made by ui_box_create(), with its descriptor (user data, in
 * the LVGL pool: lv_obj_delete() alone would leak it on every delete).
 */
void ui_box_delete(lv_obj_t* box)
{
	if (box == NULL) {
		return;
	}
	box_dsc_t* b = lv_obj_get_user_data(box);
	lv_obj_set_user_data(box, NULL);
	lv_obj_delete(box);
	lv_free(b);
}

bool ui_box_update(lv_obj_t* box, const char* caption, const char* text, const char* unit, int x, int y,
                   const ui_box_fonts_t* fonts, int width, int align, int boxtype) {
	if (boxtype == 0)
		lv_obj_add_style(box, &style_underline, 0);
	if (boxtype == 1)
		lv_obj_remove_style(box, &style_underline, 0);

	/* Fonts first: the unit drawn here used the previous (initially the
	 * style's) size, unlike every later render of it. */
	ui_box_set_fonts(box, fonts, boxtype);
	bool drawn = ui_box_set_caption(box, caption);
	drawn = ui_box_set_text(box, text) && drawn;
	drawn = ui_box_set_hint(box, unit) && drawn;
	lv_obj_set_align(box, align == 0 ? LV_ALIGN_TOP_LEFT : align);

	lv_obj_set_pos(box, x * 320 / 100, y * 240 / 100);
	lv_obj_set_width(box, width * 320 / 100);
	return drawn;
}


bool ui_box_set_caption(lv_obj_t* box, const char* caption)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	/* Its own font: "labelfont" / "labelfontsize", else display.caption
	 * (by default system bold 14; legacy step 2 = 18 px) */
	const style_props_t* r = ui_style_resolved(STYLE_EL_DISPLAY_CAPTION);
	const bool drawn = ui_render_text(b->title, caption, ui_style_get(STYLE_EL_DISPLAY_CAPTION),
	                                  box_logic_base_face(b->fonts.caption_face, r),
	                                  box_logic_base_px(b->fonts.caption_fontsize, true, r), true);
    /* An empty caption takes no room: hidden flex children get no column
     * gap either (the theme's ~8 px per gap pushed text past the box). */
    lv_obj_set_hidden(b->title, caption == NULL || caption[0] == '\0');
    return drawn;
}

bool ui_box_set_text(lv_obj_t* box, const char* text)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	const style_props_t* r = ui_style_resolved(STYLE_EL_DISPLAY_VALUE);
	return ui_render_text(b->text, text, ui_style_get(STYLE_EL_DISPLAY_VALUE),
	                      box_logic_base_face(b->fonts.face, r),
	                      box_logic_base_px(b->fonts.fontsize, false, r), false);
}

bool ui_box_set_hint(lv_obj_t* box, const char* hint)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	/* The item's "font" / "fontsize" are the value's and the unit's; without
	 * them the unit has display.unit's font / size where "style" sets them,
	 * else the value's. */
	const style_props_t* v = ui_style_resolved(STYLE_EL_DISPLAY_VALUE);
	const style_props_t* u = ui_style_resolved(STYLE_EL_DISPLAY_UNIT);
	const int face = box_logic_unit_face(b->fonts.face, v, u);
	const int px = box_logic_unit_px(b->fonts.fontsize, v, u);
	const bool drawn = ui_render_text(b->hint, hint, ui_style_get(STYLE_EL_DISPLAY_UNIT), face, px, false);
    /* An empty unit takes no room (see ui_box_set_caption). */
    lv_obj_set_hidden(b->hint, hint == NULL || hint[0] == '\0');
    return drawn;
}

void ui_box_set_fonts(lv_obj_t* box, const ui_box_fonts_t* fonts, int boxtype)
{
	box_dsc_t * b = lv_obj_get_user_data(box);
	if (b == NULL || fonts == NULL) {
		return;
	}

    b->fonts = *fonts;

	int caption_pad;
	int box_pad;
	const int step = box_logic_padding_step(fonts->fontsize, ui_style_resolved(STYLE_EL_DISPLAY_VALUE)->px);
	if (box_logic_padding(step, boxtype, &caption_pad, &box_pad)) {
		lv_obj_set_style_pad_bottom(b->title, caption_pad, 0);
		lv_obj_set_style_pad_bottom(b->main, box_pad, 0);
	} else {
		/* none (as before); a font set at runtime may have left one */
		lv_obj_remove_local_style_prop(b->title, LV_STYLE_PAD_BOTTOM, 0);
		lv_obj_remove_local_style_prop(b->main, LV_STYLE_PAD_BOTTOM, 0);
	}
}

void ui_image_set_raster(lv_obj_t* img, const lv_image_dsc_t* dsc, lv_color_t tint)
{
	if (dsc != NULL && dsc->header.cf == LV_COLOR_FORMAT_A8) {
		/* A8 is coverage only: LVGL draws it in the recolour colour */
		lv_obj_set_style_image_recolor(img, tint, 0);
		lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
	} else {
		lv_obj_remove_local_style_prop(img, LV_STYLE_IMAGE_RECOLOR_OPA, 0);
		lv_obj_remove_local_style_prop(img, LV_STYLE_IMAGE_RECOLOR, 0);
	}
	/* NULL: LVGL resets the image (nothing drawn, size 0) */
	lv_image_set_src(img, dsc);
}

bool ui_box_has_icon(lv_obj_t* box)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	return b != NULL && b->icon != NULL;
}

lv_color_t ui_box_text_color(lv_obj_t* box)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	return lv_obj_get_style_text_color(b->text, 0);
}

bool ui_box_set_icon(lv_obj_t* box, const lv_image_dsc_t* dsc, lv_color_t tint)
{
	box_dsc_t* b = lv_obj_get_user_data(box);
	if (b == NULL) {
		return false;
	}
	if (dsc == NULL) {
		if (b->icon != NULL) {
			lv_obj_delete(b->icon);
			b->icon = NULL;
			/* the value starts the second row again */
			lv_obj_set_flex_in_new_track(b->text, true);
		}
		return true;
	}
	if (b->icon == NULL) {
		lv_obj_t* icon = lv_image_create(box);
		if (icon == NULL) {
			return false;
		}
		/* After the caption, starting the value's row: [icon][value][unit] */
		lv_obj_move_to_index(icon, 1);
		lv_obj_set_flex_in_new_track(icon, true);
		lv_obj_set_flex_in_new_track(b->text, false);
		b->icon = icon;
	}
	ui_image_set_raster(b->icon, dsc, tint);
	return true;
}
