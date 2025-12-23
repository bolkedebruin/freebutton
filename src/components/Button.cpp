//
// Button - Example button component using TopicHandler
// Created by Bolke de Bruin on 19/11/2025.
//

#include "Button.h"
#include "mqtt/Registry.h"
#include "drivers/LvglDisplay.h"
#include "types.h"
#include <esp_log.h>

#include <utility>

namespace freebutton {

unsigned long Button::next_id = 1;

Button::Button(std::string label, std::string  topLabel,
               const uint32_t ledColorFront, const uint32_t ledColorWall,
               std::vector<Topic> topicConfigs)
    : id(next_id++)
    , label(std::move(label)), topLabel(std::move(topLabel))
    , ledColorFront(ledColorFront), ledColorWall(ledColorWall)
{
    topics = std::move(topicConfigs);

    // Set default values for manual button creation
    position_ = id;
    page_ = 1;
    brokerId_ = 0;

}

Button::~Button() {
    // Destroy UI button first, then unregister subscriptions
    destroyButton();
    unregisterTopics();
}

void Button::registerTopics() {
    for (const auto& topic : topics) {
        switch (topic.eventType) {
            case LABEL:
            case TOP_LABEL:
            case LED_RGB:
            case LED_FRONT_RGB:
            case LED_WALL_RGB:
                addSubscription(topic.brokerId, topic.topic);
                break;

            case CLICK:
            case LONG_PRESS:
            case RELEASE:
                addPublication(topic.brokerId, topic.topic, 0, false);
                break;

            default:
                // Default to subscription for unknown types
                addSubscription(topic.brokerId, topic.topic);
                break;
        }
    }
}

void Button::onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) {
    if (!button) {
        ESP_LOGD(TAG, "No UI button for Button %lu (no display available), ignoring UI message", id);
        return;
    }

    for (const auto& topicConfig : topics) {
        if (brokerId == topicConfig.brokerId && topic == topicConfig.topic) {
            ESP_LOGI(TAG, "Button %lu: topic=%.*s value=%.*s eventType=%d",
                     id, (int)topic.length(), topic.data(), (int)data.length(), data.data(), topicConfig.eventType);

            // Lock screen access for thread safety
            auto& lvglDisplay = FreeButton::getLvglDisplay();
            lvglDisplay.lock();

            // No need to assign labels / top labels as these are copied by lvgl
            // and no need to spend the memory
            std::string value(data.data(), data.size());

            // Handle specific message types and update UI
            switch (topicConfig.eventType) {
                case LABEL:
                    ui_button_set_text(button, value.c_str());
                    ESP_LOGI(TAG, "Button %lu label updated to: %s", id, label.c_str());
                    break;

                case TOP_LABEL:
                    ui_button_set_title(button, value.c_str());
                    ESP_LOGI(TAG, "Button %lu top label updated to: %s", id, topLabel.c_str());
                    break;

                case LED_FRONT_RGB:
                case LED_WALL_RGB:
                    // LED messages will be handled by physical LEDs managed by ButtonManager
                    ESP_LOGD(TAG, "Button %lu: LED message ignored (handled by ButtonManager): %s", id, value.c_str());
                    break;

                default:
                    ESP_LOGW(TAG, "Button %lu: Unknown event type: %d", id, static_cast<int>(topicConfig.eventType));
                    break;
            }

            lvglDisplay.unlock();
            break;
        }
    }
}

const std::string Button::getComponentType() const {
    return "button";
}

void Button::click() const {
    publishToTopic(CLICK, CLICK_PAYLOAD);
    ESP_LOGI(TAG, "Button %lu clicked", id);
}

void Button::longPress() const {
    publishToTopic(LONG_PRESS, LONG_PRESS_PAYLOAD);
    ESP_LOGI(TAG, "Button %lu long pressed", id);
}

void Button::release() const {
    publishToTopic(RELEASE, RELEASE_PAYLOAD);
    ESP_LOGI(TAG, "Button %lu released", id);
}

void Button::handlePhysicalEvent(EventType event) {
    ESP_LOGI(TAG, "Button %lu handling physical event: %d", id, static_cast<int>(event));

    switch (event) {
        case CLICK:
            click();
            break;
        case LONG_PRESS:
            longPress();
            break;
        case RELEASE:
            release();
            break;
        default:
            ESP_LOGW(TAG, "Button %lu: Unknown physical event: %d", id, static_cast<int>(event));
            break;
    }
}

void Button::unregisterTopics() {
    // Clear all current subscriptions and publications using the new clear methods
    clearSubscriptions();
    clearPublications();
}

// handleTopics is now automatically provided by TopicHandler template

void Button::publishToTopic(const EventType eventType, const std::string& payload) const {
    for (const auto& topic : topics) {
        if (topic.eventType == eventType) {
            const std::string& actualPayload = topic.payload.empty() ? payload : topic.payload;
            publish(topic.brokerId, topic.topic, actualPayload, 0, false);

            ESP_LOGD(TAG, "Button %lu published to topic %s: %s",
                    id, topic.topic.c_str(), actualPayload.c_str());
            break;
        }
    }
}

void Button::createButton() {
    // Get the appropriate small display for this button
    // Button ID maps to small display index
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lv_display_t* smallDisplay = lvglDisplay.getSmallDisplay(id);

    if (!smallDisplay) {
        ESP_LOGD(TAG, "Small display %lu not available for Button %lu, skipping UI creation", id, id);
        return;
    }

    // Acquire buffer for the small display
    if (!lvglDisplay.acquireSmallDisplayBuffer(id)) {
        ESP_LOGW(TAG, "Failed to acquire buffer for small display %lu, Button %lu", id, id);
        return;
    }

    // Save current default display and temporarily set to small display
    lv_display_t* original_default_disp = lv_display_get_default();
    lv_display_set_default(smallDisplay);

    // Get the screen from the small display
    lv_obj_t* parent = lv_display_get_screen_active(smallDisplay);

    if (!parent) {
        ESP_LOGE(TAG, "Screen not available for Button %lu on small display %lu", id, id);
        lv_display_set_default(original_default_disp);
        lvglDisplay.releaseSmallDisplayBuffer(id);
        return;
    }

    // Lock screen access for thread safety
    lvglDisplay.lock();

    // If button already exists, just redraw it with new configuration
    if (button != nullptr) {
        ESP_LOGI(TAG, "Redrawing existing button for Button %lu", id);
        ui_button_set_title(button, topLabel.c_str());
        ui_button_set_text(button, label.c_str());
        lvglDisplay.unlock();
        return;
    }

    // Initialize button styles if not already done (should be done once globally)
    static bool styles_initialized = false;
    if (!styles_initialized) {
        ui_button_styles();
        styles_initialized = true;
    }

    // Create the button with the correct small display parent
    button = ui_button_create(parent);
    if (!button) {
        ESP_LOGE(TAG, "Failed to create UI button for Button %lu", id);
        lvglDisplay.unlock();
        lvglDisplay.releaseSmallDisplayBuffer(id);
        lv_display_set_default(original_default_disp);
        return;
    }

    // Apply the initial configuration
    ui_button_set_title(button, topLabel.c_str());
    ui_button_set_text(button, label.c_str());

    // Force refresh the small display
    lv_obj_invalidate(parent);

    ESP_LOGI(TAG, "Created UI button for Button %lu on small display %lu", id, id);

    lvglDisplay.unlock();

    // Release the buffer after setting up the UI
    lvglDisplay.releaseSmallDisplayBuffer(id);

    // Restore original default display
    lv_display_set_default(original_default_disp);
}

void Button::destroyButton() {
    if (button == nullptr) {
        return;
    }

    // Lock screen access for thread safety
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lvglDisplay.lock();

    lv_obj_del(button);
    button = nullptr;
    ESP_LOGI(TAG, "Destroyed UI button for Button %lu", id);

    lvglDisplay.unlock();
}


} // namespace freebutton
