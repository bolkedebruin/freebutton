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
 * @brief Set default title color for all buttons
 * @param color Hexadecimal color value (e.g., 0xFF0000 for red)
 */
void ui_button_set_default_title_color(long color);

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
 * @brief Set the icon of a button
 * @param button Button object created with ui_button_create()
 * @param icon Icon string or symbol to display
 * @note Currently not implemented
 */
void ui_button_set_icon(lv_obj_t *button, const char *icon);

#ifdef __cplusplus
}
#endif