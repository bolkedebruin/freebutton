#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

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
    const lv_font_t *title_font; /**< ui_button_set_fonts(); NULL = the shared style */
    const lv_font_t *text_font;
    int16_t title_face, title_px; /**< base font of the top label's spans (ui_button_set_base_fonts) */
    int16_t text_face, text_px;
    bool icon_shown;              /**< the icon replaces the bottom text */
    /* Named styles of the two parts (ui_style.h ref, 0 = none): what the
     * two labels carry (ui_button_set_styles(); also each label's user
     * data), and what a row of spans is given. style_pending: a label or a
     * row could not take its style for lack of LVGL pool.
     * (Three bytes of the struct's padding.) */
    uint8_t title_style, text_style;
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
 * @brief The face (fonts.h) and px of the bottom and the top label: what a
 *        span of a text with markup starts from (\s and \f change it).
 *        Call it before the texts are set (LVGL lock held).
 */
void ui_button_set_base_fonts(lv_obj_t *button, int text_face, int text_px, int title_face, int title_px);

/**
 * @brief The named styles of the bottom and the top label (ui_style.h ref,
 *        0 = none): added to the labels after the key.label / key.toplabel
 *        style, and to the rows of spans when the texts are set. Call it
 *        before the texts are set (LVGL lock held).
 *        A style the LVGL pool has no room for is not added: see
 *        ui_button_get_styles() and ui_button_styles_pending().
 * @return true when the caller has to set the texts again (a row of spans
 *         is drawn with the style it was rendered with): a label's style
 *         changed, or a style was pending before this call.
 */
bool ui_button_set_styles(lv_obj_t *button, uint8_t text_style, uint8_t title_style);

/**
 * @brief The named styles the two labels carry now (ui_style.h ref, 0 =
 *        none): what ui_button_set_styles() asked for, except a style the
 *        LVGL pool had no room for. The caller computes the labels' fonts
 *        from these, so that font and colour are those of one look.
 */
void ui_button_get_styles(lv_obj_t *button, uint8_t *text_style, uint8_t *title_style);

/**
 * @brief True when a label or a row of spans could not take its named style
 *        for lack of LVGL pool since the last ui_button_set_styles(): the
 *        caller calls ui_button_set_styles() again later (once per walk of
 *        the pending renders at most) and sets the texts again.
 */
bool ui_button_styles_pending(lv_obj_t *button);

/**
 * @brief Fonts of the bottom label and the top label (LVGL lock held)
 * @param button Button object created with ui_button_create()
 * @param text_font Bottom label font (a local style), or NULL for the
 *        shared key.label style, or the label's named style where that holds a
 *        font (ui_style.h)
 * @param title_font Top label font, or NULL for the shared key.toplabel
 *        style (or the named style)
 */
void ui_button_set_fonts(lv_obj_t *button, const lv_font_t *text_font, const lv_font_t *title_font);

/**
 * @brief Show a raster icon in the button's bottom area, instead of the
 *        bottom label (ui/MediaImage.h; LVGL lock held)
 * @param button Button object created with ui_button_create()
 * @param dsc Raster to show at its own size, or NULL to hide the icon and
 *        show the label again. An A8 raster is tinted in the label colour.
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