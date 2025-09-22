//
// Created by Claude Code on 22/09/2025.
//

#include "wifi_qr_modal.h"
#include "wifi_qr.h"
#include <esp_log.h>

static const char* TAG = "wifi_qr_modal";

static lv_obj_t* modal = nullptr;
static lv_obj_t* qr_code = nullptr;
static lv_style_t modal_style;
static bool style_initialized = false;

void show_wifi_qr_modal(lv_obj_t* screen, const char* ssid, const char* password) {
    if (!screen || !ssid) {
        ESP_LOGE(TAG, "Invalid parameters: screen=%p, ssid=%p", (void*)screen, (void*)ssid);
        return;
    }

    // Validate screen is still valid
    if (!lv_obj_is_valid(screen)) {
        ESP_LOGE(TAG, "Invalid screen object");
        return;
    }

    // Hide existing modal if any
    hide_wifi_qr_modal();

    ESP_LOGI(TAG, "Showing WiFi QR modal for SSID: %s", ssid);

    // Initialize modal style only once
    if (!style_initialized) {
        lv_style_init(&modal_style);
        lv_style_set_bg_opa(&modal_style, LV_OPA_90);
        lv_style_set_bg_color(&modal_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&modal_style, 2);
        lv_style_set_border_color(&modal_style, lv_color_hex(0x666666));
        lv_style_set_radius(&modal_style, 8);
        style_initialized = true;
    }

    // Create modal container with fixed size to prevent overflow
    modal = lv_obj_create(screen);
    if (!modal) {
        ESP_LOGE(TAG, "Failed to create modal container");
        return;
    }

    lv_obj_move_foreground(modal);
    lv_obj_set_size(modal, 280, 200);  // Fixed size that fits 320x240 display
    lv_obj_center(modal);
    lv_obj_add_style(modal, &modal_style, 0);
    lv_obj_set_scrollbar_mode(modal, LV_SCROLLBAR_MODE_OFF);  // Disable scrollbars

    // Create main container with flex layout
    lv_obj_t* container = lv_obj_create(modal);
    if (!container) {
        ESP_LOGE(TAG, "Failed to create container");
        lv_obj_delete(modal);
        modal = nullptr;
        return;
    }
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container, 8, 0);  // Reduced padding
    lv_obj_set_style_pad_gap(container, 8, 0);  // Reduced gap
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);  // Disable scrollbars

    // Create title label
    lv_obj_t* title_label = lv_label_create(container);
    if (title_label) {
        lv_label_set_text(title_label, "WiFi Setup Required");
        lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    }

    // Create instruction label
    lv_obj_t* instruction_label = lv_label_create(container);
    if (instruction_label) {
        lv_label_set_text(instruction_label, "Scan QR code to connect to:");
        lv_obj_set_style_text_color(instruction_label, lv_color_hex(0xCCCCCC), 0);
        lv_label_set_long_mode(instruction_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(instruction_label, LV_PCT(90));
        lv_obj_set_style_text_align(instruction_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    // Create SSID label
    lv_obj_t* ssid_label = lv_label_create(container);
    if (ssid_label) {
        lv_label_set_text_fmt(ssid_label, "\"%s\"", ssid);
        lv_obj_set_style_text_color(ssid_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
    }

    // Create QR code (sized to fit in container without overflow)
    int16_t qr_size = 100; // Reduced size to ensure it fits with labels
    qr_code = create_wifi_qr_code(container, ssid, password, "WPA", false, qr_size);

    if (!qr_code) {
        ESP_LOGE(TAG, "Failed to create QR code");
        // Fallback to text message
        lv_obj_t* error_label = lv_label_create(container);
        if (error_label) {
            lv_label_set_text(error_label, "QR code generation failed\nUse web portal instead");
            lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF6666), 0);
            lv_label_set_long_mode(error_label, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(error_label, LV_PCT(90));
            lv_obj_set_style_text_align(error_label, LV_TEXT_ALIGN_CENTER, 0);
        }
    }


    ESP_LOGI(TAG, "WiFi QR modal displayed successfully");
}

void hide_wifi_qr_modal(void) {
    if (modal && lv_obj_is_valid(modal)) {
        ESP_LOGI(TAG, "Hiding WiFi QR modal");
        lv_obj_delete(modal);
        modal = nullptr;
        qr_code = nullptr;
    } else if (modal) {
        ESP_LOGW(TAG, "Modal object exists but is invalid, clearing reference");
        modal = nullptr;
        qr_code = nullptr;
    }
}