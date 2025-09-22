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

void show_wifi_qr_modal(lv_obj_t* screen, const char* ssid, const char* password) {
    if (!screen || !ssid) {
        ESP_LOGE(TAG, "Invalid parameters");
        return;
    }

    // Hide existing modal if any
    hide_wifi_qr_modal();

    ESP_LOGI(TAG, "Showing WiFi QR modal for SSID: %s", ssid);

    // Initialize modal style
    lv_style_init(&modal_style);
    lv_style_set_bg_opa(&modal_style, LV_OPA_90);
    lv_style_set_bg_color(&modal_style, lv_color_hex(0x000000));
    lv_style_set_border_width(&modal_style, 2);
    lv_style_set_border_color(&modal_style, lv_color_hex(0x666666));
    lv_style_set_radius(&modal_style, 8);

    // Create modal container
    modal = lv_obj_create(screen);
    lv_obj_move_foreground(modal);
    lv_obj_set_size(modal, LV_PCT(90), LV_PCT(90));
    lv_obj_center(modal);
    lv_obj_add_style(modal, &modal_style, 0);

    // Create main container with flex layout
    lv_obj_t* container = lv_obj_create(modal);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container, 10, 0);
    lv_obj_set_style_pad_gap(container, 15, 0);

    // Create title label
    lv_obj_t* title_label = lv_label_create(container);
    lv_label_set_text(title_label, "WiFi Setup Required");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);

    // Create instruction label
    lv_obj_t* instruction_label = lv_label_create(container);
    lv_label_set_text(instruction_label, "Scan QR code to connect to:");
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_long_mode(instruction_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(instruction_label, LV_PCT(90));
    lv_obj_set_style_text_align(instruction_label, LV_TEXT_ALIGN_CENTER, 0);

    // Create SSID label
    lv_obj_t* ssid_label = lv_label_create(container);
    lv_label_set_text_fmt(ssid_label, "\"%s\"", ssid);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);

    // Create QR code (calculate size to fit in remaining space)
    int16_t qr_size = 120; // Base size for 320x240 display
    qr_code = create_wifi_qr_code(container, ssid, password, "WPA", false, qr_size);

    if (!qr_code) {
        ESP_LOGE(TAG, "Failed to create QR code");
        // Fallback to text message
        lv_obj_t* error_label = lv_label_create(container);
        lv_label_set_text(error_label, "QR code generation failed\nUse web portal instead");
        lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF6666), 0);
        lv_label_set_long_mode(error_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(error_label, LV_PCT(90));
        lv_obj_set_style_text_align(error_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    // Create bottom instruction label
    lv_obj_t* bottom_label = lv_label_create(container);
    lv_label_set_text(bottom_label, "Or visit 192.168.4.1");
    lv_obj_set_style_text_color(bottom_label, lv_color_hex(0x999999), 0);
    lv_label_set_long_mode(bottom_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(bottom_label, LV_PCT(90));
    lv_obj_set_style_text_align(bottom_label, LV_TEXT_ALIGN_CENTER, 0);

    ESP_LOGI(TAG, "WiFi QR modal displayed successfully");
}

void hide_wifi_qr_modal(void) {
    if (modal) {
        ESP_LOGI(TAG, "Hiding WiFi QR modal");
        lv_obj_delete(modal);
        modal = nullptr;
        qr_code = nullptr;
    }
}