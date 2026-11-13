//
// Created by Claude Code on 24/12/2025.
//

#include "ota_modal.h"
#include <esp_log.h>
#include <atomic>
#include <cstring>

static const char* TAG = "ota_modal";

static lv_obj_t* modal = nullptr;
static lv_obj_t* message_label = nullptr;
static lv_obj_t* progress_bar = nullptr;
static lv_style_t modal_style;
static bool style_initialized = false;

// Thread-safe flags for pending operations
static std::atomic<bool> hide_pending{false};
static std::atomic<bool> update_pending{false};
static std::atomic<int> pending_percentage{0};

// Message buffer for thread-safe updates
static char pending_message[128] = {0};
static std::atomic<bool> message_updated{false};

// Forward declaration
static void hide_ota_modal_internal(void);

void show_ota_modal(lv_obj_t* screen, const char* message) {
    if (!screen) {
        ESP_LOGE(TAG, "Invalid parameters: screen=%p", (void*)screen);
        return;
    }

    // Validate screen is still valid
    if (!lv_obj_is_valid(screen)) {
        ESP_LOGE(TAG, "Invalid screen object");
        return;
    }

    // Clear any pending operations and hide existing modal directly
    hide_pending.store(false, std::memory_order_release);
    update_pending.store(false, std::memory_order_release);
    message_updated.store(false, std::memory_order_release);
    hide_ota_modal_internal();

    ESP_LOGI(TAG, "Showing OTA modal: %s", message ? message : "(no message)");

    // Initialize modal style only once
    if (!style_initialized) {
        lv_style_init(&modal_style);
        lv_style_set_bg_opa(&modal_style, LV_OPA_COVER);
        lv_style_set_bg_color(&modal_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&modal_style, 2);
        lv_style_set_border_color(&modal_style, lv_color_hex(0x666666));
        lv_style_set_radius(&modal_style, 8);
        style_initialized = true;
    }

    // Create modal container
    modal = lv_obj_create(screen);
    if (!modal) {
        ESP_LOGE(TAG, "Failed to create modal container");
        return;
    }

    lv_obj_move_foreground(modal);
    lv_obj_set_size(modal, 280, 140);  // Fixed size for 320x240 display
    lv_obj_center(modal);
    lv_obj_add_style(modal, &modal_style, 0);
    lv_obj_set_scrollbar_mode(modal, LV_SCROLLBAR_MODE_OFF);

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
    lv_obj_set_style_pad_all(container, 12, 0);
    lv_obj_set_style_pad_gap(container, 12, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);

    // Create title label
    lv_obj_t* title_label = lv_label_create(container);
    if (title_label) {
        lv_label_set_text(title_label, "OTA Update");
        lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(title_label, LV_FONT_DEFAULT, 0);
    }

    // Create message label
    message_label = lv_label_create(container);
    if (message_label) {
        lv_label_set_text(message_label, message ? message : "Initializing...");
        lv_obj_set_style_text_color(message_label, lv_color_hex(0xCCCCCC), 0);
        lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(message_label, LV_PCT(90));
        lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    // Create progress bar
    progress_bar = lv_bar_create(container);
    if (progress_bar) {
        lv_obj_set_size(progress_bar, LV_PCT(90), 20);
        lv_bar_set_range(progress_bar, 0, 100);
        lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    }

    // Create percentage label
    lv_obj_t* percent_label = lv_label_create(container);
    if (percent_label) {
        lv_label_set_text(percent_label, "0%");
        lv_obj_set_style_text_color(percent_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(percent_label, LV_FONT_DEFAULT, 0);
    }

    ESP_LOGI(TAG, "OTA modal displayed successfully");
}

void update_ota_modal(int percentage, const char* message) {
    // Thread-safe: Set atomic flags and copy data
    pending_percentage.store(percentage, std::memory_order_release);

    if (message) {
        strncpy(pending_message, message, sizeof(pending_message) - 1);
        pending_message[sizeof(pending_message) - 1] = '\0';
        message_updated.store(true, std::memory_order_release);
    }

    update_pending.store(true, std::memory_order_release);
}

static void hide_ota_modal_internal(void) {
    if (modal) {
        if (lv_obj_is_valid(modal)) {
            ESP_LOGI(TAG, "Hiding OTA modal");
            // Clear references first to prevent race conditions
            lv_obj_t* modal_to_delete = modal;
            modal = nullptr;
            message_label = nullptr;
            progress_bar = nullptr;
            // Now safely delete the modal
            lv_obj_delete(modal_to_delete);
        } else {
            ESP_LOGW(TAG, "Modal object exists but is invalid, clearing reference");
            modal = nullptr;
            message_label = nullptr;
            progress_bar = nullptr;
        }
    }
}

void hide_ota_modal(void) {
    // Thread-safe: Use flag-based approach
    ESP_LOGI(TAG, "Scheduling OTA modal hide");
    hide_pending.store(true, std::memory_order_release);
}

void ota_modal_process_pending(void) {
    // Process update before hide
    if (update_pending.load(std::memory_order_acquire)) {
        update_pending.store(false, std::memory_order_release);

        if (modal && lv_obj_is_valid(modal)) {
            int percentage = pending_percentage.load(std::memory_order_acquire);

            // Update progress bar
            if (progress_bar && lv_obj_is_valid(progress_bar)) {
                lv_bar_set_value(progress_bar, percentage, LV_ANIM_OFF);
            }

            // Update message if provided
            if (message_updated.load(std::memory_order_acquire)) {
                message_updated.store(false, std::memory_order_release);
                if (message_label && lv_obj_is_valid(message_label)) {
                    lv_label_set_text(message_label, pending_message);
                }
            }

            // Update percentage label (find it as child of modal's container)
            lv_obj_t* container = lv_obj_get_child(modal, 0);
            if (container && lv_obj_is_valid(container)) {
                uint32_t child_count = lv_obj_get_child_count(container);
                if (child_count >= 4) {  // title, message, progress_bar, percent_label
                    lv_obj_t* percent_label = lv_obj_get_child(container, 3);
                    if (percent_label && lv_obj_is_valid(percent_label)) {
                        lv_label_set_text_fmt(percent_label, "%d%%", percentage);
                    }
                }
            }
        }
    }

    // Process hide
    if (hide_pending.load(std::memory_order_acquire)) {
        hide_pending.store(false, std::memory_order_release);
        hide_ota_modal_internal();
    }
}
