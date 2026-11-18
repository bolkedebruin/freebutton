#include <stdio.h>
#include <string.h>
#include "ui_button.h"
#include "ui.h"
#include "lvgl.h"
#include "ui_style.h"
#include "ui_text.h"
#include "text_spans.h"


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
    b->rich_title = NULL;
    b->rich_text = NULL;
    b->title_font = NULL;
    b->text_font = NULL;
    b->title_face = (int16_t)ui_style_resolved(STYLE_EL_KEY_TOPLABEL)->face;
    b->title_px = (int16_t)ui_style_resolved(STYLE_EL_KEY_TOPLABEL)->px;
    b->text_face = (int16_t)ui_style_resolved(STYLE_EL_KEY_LABEL)->face;
    b->text_px = (int16_t)ui_style_resolved(STYLE_EL_KEY_LABEL)->px;
    b->icon_shown = false;
    b->title_style = b->text_style = 0;
    b->style_pending = false;

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


static void set_label_font(lv_obj_t* label, const lv_font_t* font)
{
    if (font != NULL) {
        lv_obj_set_style_text_font(label, font, 0);
    } else {
        lv_obj_remove_local_style_prop(label, LV_STYLE_TEXT_FONT, 0);
    }
}

/*
 * Draw a label text with its markup (text_spans.h). One format (the usual
 * case, also "\cRRGGBB" before the whole text): the label draws the text, as
 * before (it wraps and centres), with the span's colour, font and alignment
 * as local styles. Several formats: a row of labels (ui_render_text) in a
 * container beside the label, which is hidden; such a text does not wrap.
 * Returns the length of the text without its markup.
 */
static size_t set_markup(button_dsc_t * b, const bool title, const char * raw)
{
    lv_obj_t * label = title ? b->title : b->text;
    lv_obj_t ** rich = title ? &b->rich_title : &b->rich_text;
    const style_element_t el = title ? STYLE_EL_KEY_TOPLABEL : STYLE_EL_KEY_LABEL;
    const int face = title ? b->title_face : b->text_face;
    const int px = title ? b->title_px : b->text_px;
    const uint8_t named = title ? b->title_style : b->text_style;
    const bool hidden = !title && b->icon_shown;

    char in[UI_BUTTON_TEXT_MAX + 1];
    char out[UI_BUTTON_TEXT_MAX + 1];
    size_t len = raw != NULL ? strlen(raw) : 0;
    if (len > UI_BUTTON_TEXT_MAX) {
        len = UI_BUTTON_TEXT_MAX;
    }
    memcpy(in, raw != NULL ? raw : "", len);
    in[len] = '\0';
    text_span_t spans[UI_TEXT_MAX_SPANS];
    const text_parse_result_t parsed = text_parse(in, len, out, spans, UI_TEXT_MAX_SPANS, ui_text_resolve_font, NULL);

    if (parsed.span_count > 1) {
        if (*rich == NULL) {
            lv_obj_t * r = lv_obj_create(lv_obj_get_parent(label));
            if (r != NULL) {
                lv_obj_set_scrollable(r, false);
                lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
                lv_obj_set_style_bg_opa(r, 0, 0);
                lv_obj_set_style_border_width(r, 0, 0);
                lv_obj_set_style_pad_all(r, 0, 0);
                lv_obj_add_style(r, ui_style_get(el), 0); /* the base colour of the spans */
                *rich = r;
            }
        }
        if (*rich != NULL) {
            /* `named` is what the label carries, and face / px follow it
             * (Button::applyFonts). A row the pool has no room to add it
             * to is drawn without its colour for now. */
            if (!ui_text_set_named(*rich, named)) {
                b->style_pending = true;
            }
            ui_render_text(*rich, in, ui_style_get(el), ui_text_named(*rich), face, px, title);
            lv_obj_set_hidden(*rich, hidden);
            lv_obj_set_hidden(label, true);
            return parsed.text_len;
        }
        /* No memory for the row: fall through, the text in the first format. */
    }

    if (*rich != NULL) {
        lv_obj_delete(*rich);
        *rich = NULL;
    }
    lv_obj_set_hidden(label, hidden);
    lv_label_set_text(label, out);

    const text_span_t * s = parsed.span_count > 0 ? &spans[0] : NULL;
    if (s != NULL && s->has_color) {
        lv_obj_set_style_text_color(label, lv_color_hex(s->color), 0);
    } else {
        lv_obj_remove_local_style_prop(label, LV_STYLE_TEXT_COLOR, 0);
    }
    if (s != NULL && (s->font >= 0 || s->size_kind != TEXT_SIZE_BASE)) {
        lv_obj_set_style_text_font(label, ui_text_span_font(s, face, px, title), 0);
    } else {
        set_label_font(label, title ? b->title_font : b->text_font);
    }
    if (s != NULL && s->align != TEXT_ALIGN_KEEP) {
        lv_obj_set_style_text_align(label,
                                    s->align == TEXT_ALIGN_LEFT ? LV_TEXT_ALIGN_LEFT
                                    : s->align == TEXT_ALIGN_RIGHT ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
    } else {
        lv_obj_remove_local_style_prop(label, LV_STYLE_TEXT_ALIGN, 0);
    }
    return parsed.text_len;
}

void ui_button_set_title(lv_obj_t * button, const char * title)
{
	button_dsc_t * b = (button_dsc_t *)lv_obj_get_user_data(button);
	if (b == NULL) {
		printf("ui_button_set_title: button_dsc_t is NULL\n");
		return;
	}

	const size_t shown = set_markup(b, true, title);
	//If no title provided, set height to 0, this will center remaining text
	if (shown == 0)
		lv_obj_set_size(b->title, lv_pct(100), 0);
    else
		lv_obj_set_size(b->title, lv_pct(100), LV_SIZE_CONTENT);
}

void ui_button_set_text(lv_obj_t * button, const char * text)
{
	button_dsc_t * b = (button_dsc_t *)lv_obj_get_user_data(button);
	if (b == NULL) {
		printf("ui_button_set_text: button_dsc_t is NULL\n");
		return;
	}
	set_markup(b, false, text);
}

void ui_button_set_base_fonts(lv_obj_t* button, const int text_face, const int text_px, const int title_face,
                              const int title_px)
{
    button_dsc_t* b = (button_dsc_t*)lv_obj_get_user_data(button);
    if (b == NULL) {
        return;
    }
    b->text_face = (int16_t)text_face;
    b->text_px = (int16_t)text_px;
    b->title_face = (int16_t)title_face;
    b->title_px = (int16_t)title_px;
}

/* The named style on a label, after its element style (ui_button_create),
 * so it wins over it. The ref the label carries now is its user data (0 on
 * a new label): only a change touches the object. Returns that ref: `named`,
 * or 0 when the LVGL pool had no room to add it (*pending set; nothing is
 * tried again here, see ui_button_styles_pending). */
static uint8_t set_label_style(lv_obj_t* label, const uint8_t named, bool* pending)
{
    if (!ui_text_set_named(label, named)) {
        *pending = true;
    }
    return ui_text_named(label);
}

bool ui_button_set_styles(lv_obj_t* button, const uint8_t text_style, const uint8_t title_style)
{
    button_dsc_t* b = (button_dsc_t*)lv_obj_get_user_data(button);
    if (b == NULL) {
        return false;
    }
    /* A style that was pending: the texts are set again either way (a row
     * of spans may have been the one without its style). */
    const bool was_pending = b->style_pending;
    b->style_pending = false;
    const uint8_t text = set_label_style(b->text, text_style, &b->style_pending);
    const uint8_t title = set_label_style(b->title, title_style, &b->style_pending);
    const bool changed = b->text_style != text || b->title_style != title;
    b->text_style = text;
    b->title_style = title;
    return changed || was_pending;
}

void ui_button_get_styles(lv_obj_t* button, uint8_t* text_style, uint8_t* title_style)
{
    const button_dsc_t* b = (const button_dsc_t*)lv_obj_get_user_data(button);
    *text_style = b != NULL ? b->text_style : 0;
    *title_style = b != NULL ? b->title_style : 0;
}

bool ui_button_styles_pending(lv_obj_t* button)
{
    const button_dsc_t* b = (const button_dsc_t*)lv_obj_get_user_data(button);
    return b != NULL && b->style_pending;
}

void ui_button_set_fonts(lv_obj_t* button, const lv_font_t* text_font, const lv_font_t* title_font)
{
    button_dsc_t* b = (button_dsc_t*)lv_obj_get_user_data(button);
    if (b == NULL) {
        return;
    }
    b->text_font = text_font;
    b->title_font = title_font;
    set_label_font(b->text, text_font);
    set_label_font(b->title, title_font);
}

void ui_button_set_icon(lv_obj_t* button, const lv_image_dsc_t* dsc)
{
    button_dsc_t* b = (button_dsc_t*)lv_obj_get_user_data(button);
    if (!b || !b->icon) {
        printf("ui_button_set_icon: invalid button or icon object\n");
        return;
    }
    ui_image_set_raster(b->icon, dsc, lv_obj_get_style_text_color(b->text, 0));
    b->icon_shown = dsc != NULL;
    lv_obj_set_hidden(b->icon, dsc == NULL);
    /* The bottom text is its label, or the row of a text with several formats. */
    lv_obj_set_hidden(b->rich_text != NULL ? b->rich_text : b->text, dsc != NULL);
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
