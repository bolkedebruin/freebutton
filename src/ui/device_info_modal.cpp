#include "device_info_modal.h"
#include <esp_log.h>

static const char* TAG = "device_info_modal";

static lv_obj_t* modal = nullptr;
static lv_obj_t* info_label = nullptr;
static lv_style_t modal_style;
static lv_style_t text_style;

void show_device_info_modal(lv_obj_t* screen, const char* device_info) {
    if (!screen || !device_info) {
        ESP_LOGE(TAG, "Invalid parameters");
        return;
    }

    // Hide existing modal if any
    hide_device_info_modal();

    ESP_LOGI(TAG, "Showing device info modal");

    // Initialize styles only once
    static bool styles_initialized = false;

    if (!styles_initialized) {
        lv_style_init(&modal_style);
        lv_style_set_bg_opa(&modal_style, LV_OPA_COVER);
        lv_style_set_bg_color(&modal_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&modal_style, 2);
        lv_style_set_border_color(&modal_style, lv_color_hex(0x666666));
        lv_style_set_radius(&modal_style, 8);

        lv_style_init(&text_style);
        lv_style_set_text_color(&text_style, lv_color_hex(0xffffff));
        lv_style_set_text_font(&text_style, &lv_font_montserrat_12);

        styles_initialized = true;
    }

    // Create modal container
    modal = lv_obj_create(screen);
    lv_obj_set_size(modal, 280, 200); // Fixed size instead of percentage
    lv_obj_center(modal);
    lv_obj_add_style(modal, &modal_style, 0);

    // Create info label directly without extra container
    info_label = lv_label_create(modal);
    lv_label_set_text(info_label, device_info);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(info_label, 250, 140);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_style(info_label, &text_style, 0);

    // Simple close instruction
    lv_obj_t* close_label = lv_label_create(modal);
    lv_label_set_text(close_label, "Release to close, hold on for API keys");
    lv_obj_set_style_text_color(close_label, lv_color_hex(0x888888), 0);
    lv_obj_align(close_label, LV_ALIGN_BOTTOM_MID, 0, -5);

    ESP_LOGI(TAG, "Device info modal displayed");
}

void hide_device_info_modal(void) {
    if (modal) {
        ESP_LOGI(TAG, "Hiding device info modal");
        lv_obj_del(modal);
        modal = nullptr;
        info_label = nullptr;
    }
}

bool is_device_info_modal_visible(void) {
    return modal != nullptr;
}