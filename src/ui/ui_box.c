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

    /* The item's style list as it was given (ui_box_set_styles); what
     * each part carries is its own user data (ui_text.h). */
    style_list_t styles;
} box_dsc_t;

/* A part does not carry the styles it was given: the LVGL pool had no room
 * to add them (ui_box_set_styles). The parts are drawn with the styles
 * they carry; ui_box_update() reports it and the caller calls it again
 * later. (Computed, not kept.) */
static bool styles_pending(const box_dsc_t* b)
{
	return ui_text_styles(b->title) != b->styles || ui_text_styles(b->text) != b->styles ||
	       ui_text_styles(b->hint) != b->styles;
}


static lv_style_t style_box;
static lv_style_t style_underline;

/* LVGL lock held. The text styles of caption, value and unit are
 * ui_style.h's (the displayitem elements). */
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

    b->styles = 0;
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
                   style_list_t styles, int width, int align, int boxtype) {
	if (boxtype == 0)
		lv_obj_add_style(box, &style_underline, 0);
	if (boxtype == 1)
		lv_obj_remove_style(box, &style_underline, 0);

	/* Styles first: the texts below are drawn with what the parts carry. */
	ui_box_set_styles(box, styles, boxtype);
	bool drawn = ui_box_set_caption(box, caption);
	drawn = ui_box_set_text(box, text) && drawn;
	drawn = ui_box_set_hint(box, unit) && drawn;
	const box_dsc_t* b = lv_obj_get_user_data(box);
	if (b != NULL && styles_pending(b)) {
		drawn = false; /* the styles found no room: again later */
	}
	lv_obj_set_align(box, align == 0 ? LV_ALIGN_TOP_LEFT : align);

	lv_obj_set_pos(box, x * 320 / 100, y * 240 / 100);
	lv_obj_set_width(box, width * 320 / 100);
	return drawn;
}


void ui_box_part_font(lv_obj_t* box, int part, int* face, int* px, uint8_t* align)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	style_props_t r;
	if (part == UI_BOX_UNIT) {
		/* Both lookups through what the unit carries: one look. The value's
		 * face and px, then the unit's own where a "unit" block sets one. */
		const style_list_t carried = ui_text_styles(b->hint);
		style_props_t v;
		ui_style_lookup(STYLE_KIND_DISPLAYITEM, STYLE_PART_MAIN, carried, &v, NULL);
		ui_style_lookup(STYLE_KIND_DISPLAYITEM, STYLE_PART_UNIT, carried, &r, NULL);
		style_logic_follow_font(&r, v.face, v.px, face, px);
		if (align != NULL) {
			*align = 0; /* text-align in "unit" is ignored */
		}
		return;
	}
	const bool caption = part == UI_BOX_CAPTION;
	ui_style_lookup(STYLE_KIND_DISPLAYITEM, caption ? STYLE_PART_TITLE : STYLE_PART_MAIN,
	                ui_text_styles(caption ? b->title : b->text), &r, NULL);
	*face = r.face;
	*px = r.px;
	if (align != NULL) {
		*align = style_logic_text_align(&r);
	}
}

bool ui_box_set_caption(lv_obj_t* box, const char* caption)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	int face;
	int px;
	uint8_t align;
	ui_box_part_font(box, UI_BOX_CAPTION, &face, &px, &align);
	const bool drawn = ui_render_text(b->title, caption, ui_style_get(STYLE_EL_DISPLAY_CAPTION), STYLE_PART_TITLE,
	                                  face, px, true, align);
    /* An empty caption takes no room: hidden flex children get no column
     * gap either (the theme's ~8 px per gap pushed text past the box). */
    lv_obj_set_hidden(b->title, caption == NULL || caption[0] == '\0');
    return drawn;
}

bool ui_box_set_text(lv_obj_t* box, const char* text)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	int face;
	int px;
	uint8_t align;
	ui_box_part_font(box, UI_BOX_VALUE, &face, &px, &align);
	return ui_render_text(b->text, text, ui_style_get(STYLE_EL_DISPLAY_VALUE), STYLE_PART_MAIN, face, px, false,
	                      align);
}

bool ui_box_set_hint(lv_obj_t* box, const char* hint)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	int face;
	int px;
	ui_box_part_font(box, UI_BOX_UNIT, &face, &px, NULL);
	const bool drawn = ui_render_text(b->hint, hint, ui_style_get(STYLE_EL_DISPLAY_UNIT), STYLE_PART_UNIT, face, px,
	                                  false, 0);
    /* An empty unit takes no room (see ui_box_set_caption). */
    lv_obj_set_hidden(b->hint, hint == NULL || hint[0] == '\0');
    return drawn;
}

void ui_box_set_styles(lv_obj_t* box, style_list_t list, int boxtype)
{
	box_dsc_t * b = lv_obj_get_user_data(box);
	if (b == NULL) {
		return;
	}

    b->styles = list;

	int caption_pad;
	int box_pad;
	/* The styles go on the parts here, once per call, and nowhere else:
	 * caption, value and unit are then drawn with what they carry (fonts,
	 * colour, alignment) and the padding below follows the same. The three
	 * together: when the LVGL pool has no room for all of them, none gets
	 * the new list (never a value in the new look beside a unit in the old
	 * one). */
	lv_obj_t* const objs[] = {b->title, b->text, b->hint};
	static const style_part_t parts[] = {STYLE_PART_TITLE, STYLE_PART_MAIN, STYLE_PART_UNIT};
	ui_text_set_styles_all(objs, parts, 3, list);

	/* The padding goes with the value's size, and with whether one of the
	 * item's own styles set it (box_logic.h). */
	style_props_t r;
	bool own_size;
	ui_style_lookup(STYLE_KIND_DISPLAYITEM, STYLE_PART_MAIN, ui_text_styles(b->text), &r, &own_size);
	const int step = box_logic_padding_step(r.px, own_size);
	if (box_logic_padding(step, boxtype, &caption_pad, &box_pad)) {
		lv_obj_set_style_pad_bottom(b->title, caption_pad, 0);
		lv_obj_set_style_pad_bottom(b->main, box_pad, 0);
	} else {
		/* none; an earlier style may have left one */
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

lv_color_t ui_box_icon_color(lv_obj_t* box)
{
	const box_dsc_t* b = lv_obj_get_user_data(box);
	uint32_t rgb;
	if (ui_style_icon_color(STYLE_KIND_DISPLAYITEM, ui_text_styles(b->text), &rgb)) {
		return lv_color_hex(rgb);
	}
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
