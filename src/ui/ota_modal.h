//
// Created by Claude Code on 24/12/2025.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Show OTA progress modal dialog
 * @param screen Parent screen object
 * @param message Initial status message
 */
void show_ota_modal(lv_obj_t* screen, const char* message);

/**
 * @brief Update OTA progress
 * @param percentage Progress percentage (0-100)
 * @param message Status message (can be nullptr to keep current message)
 */
void update_ota_modal(int percentage, const char* message);

/**
 * @brief Hide the OTA modal dialog
 * Thread-safe: Can be called from any thread
 */
void hide_ota_modal(void);

/**
 * @brief Process pending UI operations for OTA modal
 * Must be called from LVGL thread only
 */
void ota_modal_process_pending(void);

#ifdef __cplusplus
}
#endif
