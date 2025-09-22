//
// Created by Claude Code on 22/09/2025.
//

#include "wifi_qr.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <esp_log.h>

static const char* TAG = "wifi_qr";

// WiFi QR code format: WIFI:T:<security>;S:<ssid>;P:<password>;H:<hidden>;;
// Example: WIFI:T:WPA;S:MyNetwork;P:MyPassword;H:false;;

static char* build_wifi_qr_string(const char* ssid, const char* password, const char* security, bool hidden) {
    if (!ssid) {
        ESP_LOGE(TAG, "SSID cannot be NULL");
        return nullptr;
    }

    // Ensure we have defaults for optional parameters
    if (!password) password = "";
    if (!security) security = "";

    // Calculate required buffer size for WiFi QR string
    // Format: WIFI:T:<security>;S:<ssid>;P:<password>;H:<hidden>;;
    size_t wifi_str_len = strlen("WIFI:T:;S:;P:;H:;;") +
                         strlen(security) + strlen(ssid) + strlen(password) +
                         (hidden ? 4 : 5); // "true" or "false"

    char* wifi_str = (char*)malloc(wifi_str_len + 1);
    if (!wifi_str) {
        ESP_LOGE(TAG, "Failed to allocate memory for WiFi string");
        return nullptr;
    }

    // Build WiFi QR code string
    snprintf(wifi_str, wifi_str_len + 1, "WIFI:T:%s;S:%s;P:%s;H:%s;;",
             security, ssid, password, hidden ? "true" : "false");

    ESP_LOGI(TAG, "Generated WiFi QR string: %s", wifi_str);
    return wifi_str;
}

lv_obj_t* create_wifi_qr_code(lv_obj_t* parent, const char* ssid, const char* password, const char* security, bool hidden, int16_t size) {
    if (!parent || !ssid) {
        ESP_LOGE(TAG, "Invalid parameters: parent and ssid cannot be NULL");
        return nullptr;
    }

    // Build WiFi QR code string
    char* wifi_str = build_wifi_qr_string(ssid, password, security, hidden);
    if (!wifi_str) {
        return nullptr;
    }

    // Create QR code widget
    lv_obj_t* qr = lv_qrcode_create(parent);
    if (!qr) {
        ESP_LOGE(TAG, "Failed to create QR code widget");
        free(wifi_str);
        return nullptr;
    }

    // Set size
    lv_qrcode_set_size(qr, size);

    // Set colors (black QR code on white background)
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());

    // Generate QR code
    lv_result_t result = lv_qrcode_update(qr, wifi_str, strlen(wifi_str));
    if (result != LV_RESULT_OK) {
        ESP_LOGE(TAG, "Failed to update QR code, error: %d", result);
        lv_obj_delete(qr);
        free(wifi_str);
        return nullptr;
    }

    ESP_LOGI(TAG, "WiFi QR code created successfully");
    free(wifi_str);
    return qr;
}

bool update_wifi_qr_code(lv_obj_t* qr_obj, const char* ssid, const char* password, const char* security, bool hidden) {
    if (!qr_obj || !ssid) {
        ESP_LOGE(TAG, "Invalid parameters: qr_obj and ssid cannot be NULL");
        return false;
    }

    // Build new WiFi QR code string
    char* wifi_str = build_wifi_qr_string(ssid, password, security, hidden);
    if (!wifi_str) {
        return false;
    }

    // Update QR code
    lv_result_t result = lv_qrcode_update(qr_obj, wifi_str, strlen(wifi_str));
    if (result != LV_RESULT_OK) {
        ESP_LOGE(TAG, "Failed to update QR code, error: %d", result);
        free(wifi_str);
        return false;
    }

    ESP_LOGI(TAG, "WiFi QR code updated successfully");
    free(wifi_str);
    return true;
}