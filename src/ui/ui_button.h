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
 * @param title Title string to display (empty string hides title)
 */
void ui_button_set_title(lv_obj_t *button, const char *title);

/**
 * @brief Set the main text content of a button
 * @param button Button object created with ui_button_create()
 * @param text Text string to display
 */
void ui_button_set_text(lv_obj_t *button, const char *text);

/**
 * @brief Fonts of the bottom label and the top label (LVGL lock held)
 * @param button Button object created with ui_button_create()
 * @param text_font Bottom label font (a local style), or NULL for the
 *        shared key.label style (ui_style.h)
 * @param title_font Top label font, or NULL for the shared key.toplabel
 *        style
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