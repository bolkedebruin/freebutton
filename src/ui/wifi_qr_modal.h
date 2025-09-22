//
// Created by Claude Code on 22/09/2025.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Show a modal dialog with WiFi QR code for access point connection
 * @param screen Parent screen object
 * @param ssid The WiFi SSID to display
 * @param password The WiFi password (can be empty for open networks)
 */
void show_wifi_qr_modal(lv_obj_t* screen, const char* ssid, const char* password);

/**
 * @brief Hide the WiFi QR modal dialog
 */
void hide_wifi_qr_modal(void);

#ifdef __cplusplus
}
#endif