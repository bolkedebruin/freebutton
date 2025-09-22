//
// Created by Claude Code on 22/09/2025.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Create and display a QR code for WiFi access point connection
 * @param parent Parent LVGL object to create the QR code in
 * @param ssid The WiFi SSID
 * @param password The WiFi password (can be empty for open networks)
 * @param security Security type ("WPA", "WEP", or empty for none)
 * @param hidden Whether the network is hidden
 * @param size Size of the QR code widget in pixels
 * @return Pointer to the created QR code object, or NULL if failed
 */
lv_obj_t* create_wifi_qr_code(lv_obj_t* parent, const char* ssid, const char* password, const char* security, bool hidden, int16_t size);

/**
 * @brief Update an existing WiFi QR code with new credentials
 * @param qr_obj Existing QR code object
 * @param ssid The WiFi SSID
 * @param password The WiFi password
 * @param security Security type
 * @param hidden Whether the network is hidden
 * @return true if update was successful, false otherwise
 */
bool update_wifi_qr_code(lv_obj_t* qr_obj, const char* ssid, const char* password, const char* security, bool hidden);

#ifdef __cplusplus
}
#endif