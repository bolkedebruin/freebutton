#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Show a modal dialog with device information
 * @param screen Parent screen object
 * @param device_info Formatted device info string to display
 */
void show_device_info_modal(lv_obj_t* screen, const char* device_info);

/**
 * @brief Hide the device info modal dialog
 */
void hide_device_info_modal(void);

/**
 * @brief Check if device info modal is currently visible
 * @return true if modal is visible, false otherwise
 */
bool is_device_info_modal_visible(void);

#ifdef __cplusplus
}
#endif