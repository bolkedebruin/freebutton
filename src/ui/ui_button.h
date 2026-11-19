#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "style_logic.h"

/**
 * @brief Button descriptor structure containing UI elements
 */
typedef struct {
    lv_obj_t *main;   /**< Main container object */
    lv_obj_t *title;  /**< Title label object */
    lv_obj_t *text;   /**< Text label object */
    lv_obj_t *icon;   /**< Icon object */
    /* Text markup (text_spans.h). A text of one format is drawn by the label
     * above, with that format as local styles. A text of several formats is
     * drawn as a row of labels in these (NULL until one is needed; the label
     * is then hidden). */
    lv_obj_t *rich_title;
    lv_obj_t *rich_text;
    const lv_font_t *title_font; /**< ui_button_set_fonts(); NULL = the shared styles' */
    const lv_font_t *text_font;
    /* What a label's text starts from (ui_button_set_base): the face
     * (fonts.h) and px of its spans, and its line-height (x100, 0 = no
     * style sets one). */
    int8_t title_face, text_face;
    uint8_t title_px, text_px;
    uint16_t title_line_height, text_line_height;
    bool icon_shown;              /**< the icon replaces the bottom text */
    /* A label or a row of spans could not take the item's styles for lack
     * of LVGL pool (ui_button_set_styles). Which styles a label carries is
     * its user data (ui_text.h). */
    bool style_pending;
} button_dsc_t;

/**
 * @brief Initialize button styles
 * Must be called once before creating buttons
 */
void ui_button_styles(void);

/**
 * @brief Create a new button widget
 * @param parent Parent object to attach the button to
 * @return Pointer to the created button object, or NULL on failure
 */
lv_obj_t *ui_button_create(lv_obj_t *parent);

/**
 * @brief Set the title text of a button
 * @param button Button object created with ui_button_create()
 * @param title Title string to display (empty string hides title). Text
 *        markup applies (text_spans.h: \\cRRGGBB, \\s{px}, \\f{font}, ...).
 */
void ui_button_set_title(lv_obj_t *button, const char *title);

/**
 * @brief Set the main text content of a button
 * @param button Button object created with ui_button_create()
 * @param text Text string to display; text markup applies as for the title
 */
void ui_button_set_text(lv_obj_t *button, const char *text);

/** Longest label text drawn, markup included (bytes; a longer one is cut). */
#define UI_BUTTON_TEXT_MAX 127

/**
 * @brief What the bottom and the top label start from: the face (fonts.h)
 *        and px of a span of a text with markup (\s and \f change it), and
 *        the label's line-height (x100; 0 = none, LVGL's own line space).
 *        Call it before the texts are set (LVGL lock held).
 * @return true when one of them changed: the texts have to be set again.
 */
bool ui_button_set_base(lv_obj_t *button, int text_face, int text_px, int text_line_height, int title_face,
                        int title_px, int title_line_height);

/**
 * @brief The item's styles (ui_style.h list): the bottom label carries
 *        their main blocks, the top label their "title" blocks, after the
 *        button element styles; so do the rows of spans when the texts are
 *        set. Call it before the texts are set (LVGL lock held).
 *        Styles the LVGL pool has no room for are not added: see
 *        ui_button_get_styles() and ui_button_styles_pending().
 * @return true when the caller has to set the texts again (a row of spans
 *         is drawn with the styles it was rendered with): a label's styles
 *         changed, or styles were pending before this call.
 */
bool ui_button_set_styles(lv_obj_t *button, style_list_t styles);

/**
 * @brief The style lists the two labels carry now: what
 *        ui_button_set_styles() asked for, except where the LVGL pool had
 *        no room (the label then carries what it carried). The caller
 *        computes the labels' fonts from these, so that font and colour
 *        are those of one look.
 */
void ui_button_get_styles(lv_obj_t *button, style_list_t *text_styles, style_list_t *title_styles);

/**
 * @brief True when a label or a row of spans could not take the styles for
 *        lack of LVGL pool since the last ui_button_set_styles(): the
 *        caller calls ui_button_set_styles() again later (once per walk of
 *        the pending renders at most) and sets the texts again.
 */
bool ui_button_styles_pending(lv_obj_t *button);

/**
 * @brief Fonts of the bottom label and the top label (LVGL lock held)
 * @param button Button object created with ui_button_create()
 * @param text_font Bottom label font (a local style), or NULL when the
 *        shared styles the label carries give the font (ui_style.h)
 * @param title_font Top label font, or NULL likewise
 * @return true when one of them changed. The fonts are only kept here: a
 *         label gets its font when its text is set (ui_button_set_text,
 *         ui_button_set_title), where a font from the text's markup wins.
 *         So set the texts again after a change.
 */
bool ui_button_set_fonts(lv_obj_t *button, const lv_font_t *text_font, const lv_font_t *title_font);

/**
 * @brief Show a raster icon in the button's bottom area, instead of the
 *        bottom label (ui/MediaImage.h; LVGL lock held)
 * @param button Button object created with ui_button_create()
 * @param dsc Raster to show at its own size, or NULL to hide the icon and
 *        show the label again. An A8 raster is tinted in the "icon" colour
 *        of the button's styles, else in the label colour.
 */
void ui_button_set_icon(lv_obj_t *button, const lv_image_dsc_t *dsc);

/**
 * @brief Delete a button made by ui_button_create(), with its descriptor
 *        (user data in the LVGL pool, which lv_obj_delete() alone leaks)
 */
void ui_button_delete(lv_obj_t *button);

#ifdef __cplusplus
}
#endif