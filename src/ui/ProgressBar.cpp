#include "ProgressBar.h"
#include "../drivers/LvglDisplay.h"
#include <esp_log.h>
#include <algorithm>

namespace FreeButton::UI {

static constexpr auto TAG = "ProgressBar";

// Forward declare the global function
extern LvglDisplay& getLvglDisplay();

ProgressBar::ProgressBar(lv_obj_t* parent, const Config& config)
    : parent_(parent), config_(config) {
    if (!parent_) {
        ESP_LOGE(TAG, "Parent object cannot be null");
        return;
    }
    create_ui();
}

ProgressBar::ProgressBar(lv_obj_t* parent)
    : parent_(parent), config_{} {
    if (!parent_) {
        ESP_LOGE(TAG, "Parent object cannot be null");
        return;
    }
    create_ui();
}

ProgressBar::~ProgressBar() {
    cleanup();
}

void ProgressBar::create_ui() {
    if (!parent_) return;

    // Ensure we're properly locked before creating LVGL objects
    FreeButton::getLvglDisplay().lock();

    progress_label_ = lv_label_create(parent_);
    if (!progress_label_) {
        ESP_LOGE(TAG, "Failed to create progress label");
        FreeButton::getLvglDisplay().unlock();
        return;
    }

    lv_label_set_text(progress_label_, "");
    lv_obj_set_style_text_color(progress_label_, lv_color_hex(config_.text_color), 0);
    lv_obj_set_style_text_font(progress_label_, config_.font, 0);
    lv_obj_align(progress_label_, LV_ALIGN_CENTER, 0, config_.label_offset_y);

    progress_bar_ = lv_bar_create(parent_);
    if (!progress_bar_) {
        ESP_LOGE(TAG, "Failed to create progress bar");
        FreeButton::getLvglDisplay().unlock();
        cleanup();
        return;
    }

    lv_obj_set_size(progress_bar_, config_.width, config_.height);
    lv_obj_align(progress_bar_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(config_.bg_color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(config_.indicator_color), LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar_, config_.radius, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar_, config_.radius, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(progress_bar_, 0, LV_PART_MAIN);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);

    lv_obj_add_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(progress_label_, LV_OBJ_FLAG_HIDDEN);

    create_blur_overlay();

    FreeButton::getLvglDisplay().unlock();

    ESP_LOGD(TAG, "Progress bar UI created successfully");
}

void ProgressBar::cleanup() {
    if (progress_bar_) {
        lv_obj_del(progress_bar_);
        progress_bar_ = nullptr;
    }
    if (progress_label_) {
        lv_obj_del(progress_label_);
        progress_label_ = nullptr;
    }
    if (blur_overlay_) {
        lv_obj_del(blur_overlay_);
        blur_overlay_ = nullptr;
    }
    visible_ = false;
}

void ProgressBar::create_blur_overlay() {
    if (!parent_ || !config_.blur_background) return;

    blur_overlay_ = lv_obj_create(parent_);
    if (!blur_overlay_) {
        ESP_LOGE(TAG, "Failed to create blur overlay");
        return;
    }

    lv_obj_set_size(blur_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(blur_overlay_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(blur_overlay_, lv_color_hex(config_.blur_color), 0);
    lv_obj_set_style_bg_opa(blur_overlay_, config_.blur_opacity, 0);
    lv_obj_set_style_border_width(blur_overlay_, 0, 0);
    lv_obj_set_style_radius(blur_overlay_, 0, 0);
    lv_obj_remove_flag(blur_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(blur_overlay_, LV_OBJ_FLAG_HIDDEN);

    // Move to back so progress bar and label are on top
    lv_obj_move_to_index(blur_overlay_, 0);

    ESP_LOGD(TAG, "Blur overlay created");
}

void ProgressBar::show(std::string_view message) {
    if (!progress_bar_ || !progress_label_) return;

    FreeButton::getLvglDisplay().lock();

    lv_label_set_text(progress_label_, message.data());
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);

    if (blur_overlay_) {
        lv_obj_clear_flag(blur_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(progress_label_, LV_OBJ_FLAG_HIDDEN);

    visible_ = true;

    FreeButton::getLvglDisplay().unlock();

    ESP_LOGD(TAG, "Progress bar shown with message: %.*s",
             static_cast<int>(message.length()), message.data());
}

void ProgressBar::update(const std::int32_t percentage, std::string_view message) const {
    if (!progress_bar_ || !visible_) return;

    FreeButton::getLvglDisplay().lock();

    const auto clamped_percentage = (percentage < 0) ? 0 : (percentage > 100) ? 100 : percentage;
    lv_bar_set_value(progress_bar_, clamped_percentage, LV_ANIM_ON);

    if (!message.empty() && progress_label_) {
        lv_label_set_text(progress_label_, message.data());
    }

    FreeButton::getLvglDisplay().unlock();

    ESP_LOGD(TAG, "Progress updated: %d%% %s%.*s", clamped_percentage,
             message.empty() ? "" : "- ",
             static_cast<int>(message.length()), message.data());
}

void ProgressBar::hide() {
    if (!progress_bar_ || !progress_label_) return;

    FreeButton::getLvglDisplay().lock();

    lv_obj_add_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(progress_label_, LV_OBJ_FLAG_HIDDEN);
    if (blur_overlay_) {
        lv_obj_add_flag(blur_overlay_, LV_OBJ_FLAG_HIDDEN);
    }

    visible_ = false;

    FreeButton::getLvglDisplay().unlock();

    ESP_LOGD(TAG, "Progress bar hidden");
}

bool ProgressBar::is_visible() const noexcept {
    return visible_ && progress_bar_ && !lv_obj_has_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);
}

std::int32_t ProgressBar::get_progress() const noexcept {
    if (!progress_bar_) return 0;
    return lv_bar_get_value(progress_bar_);
}

} // namespace FreeButton::UI