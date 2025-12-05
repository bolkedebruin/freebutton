//
// Created by Bolke de Bruin on 18/11/2025.
//

#include "DisplayItem.h"
#include "mqtt/Registry.h"
#include "drivers/LvglDisplay.h"
#include "types.h"

#include <utility>

namespace freebutton {

unsigned long DisplayItem::next_id = 1;

DisplayItem::DisplayItem(const int x, const int y, const int width, const int boxtype, const int fontSize, const int alignment,
                        std::string  label, std::string  unit, const int round, const int page,
                        std::vector<Topic> topicConfigs)
    : id(next_id++)
    , x(x), y(y), width(width), boxType(boxtype)
    , fontSize(fontSize), alignment(alignment)
    , label(std::move(label)), unit(std::move(unit)), round(round), page(page)
{
    topics = std::move(topicConfigs);
    // Topics will be registered later via ComponentManager::registerAllSubscriptions()
    // UI box creation is deferred until after configuration (called from fromJson)
}

DisplayItem::~DisplayItem() {
    // Destroy UI box first, then unregister subscriptions
    destroyBox();
    unregisterTopics();
}

void DisplayItem::registerTopics() {
    for (const auto& topic : topics) {
        addSubscription(topic.brokerId, topic.topic);
    }
}

void DisplayItem::onMessage(const uint8_t brokerId, const std::string_view topic, const std::string_view data) {
    if (!box) {
        ESP_LOGW(TAG, "Box not created yet for DisplayItem %lu, ignoring message", id);
        return;
    }

    for (const auto& topicConfig : topics) {
        if (brokerId == topicConfig.brokerId && topic == topicConfig.topic) {
            ESP_LOGI(TAG, "DisplayItem %lu: topic=%.*s value=%.*s eventType=%d",
                     id, (int)topic.length(), topic.data(), (int)data.length(), data.data(), topicConfig.eventType);

            // No need to assign labels / top labels as these are copied by lvgl
            // and no need to spend the memory
            std::string value(data.data(), data.length());

            // Lock screen access for thread safety
            auto& lvglDisplay = FreeButton::getLvglDisplay();
            lvglDisplay.lock();

            // Handle different event types as specified in the requirements
            switch(topicConfig.eventType) {
                case DISPLAY_LABEL:
                    ui_box_set_caption(box, value.c_str());
                    break;
                case DISPLAY_VALUE:
                    ui_box_set_text(box, value.c_str());
                    break;
                case DISPLAY_UNIT:
                    ui_box_set_hint(box, value.c_str());
                    break;
                default:
                    ESP_LOGW(TAG, "DisplayItem %lu: Unknown event type: %d", id, topicConfig.eventType);
                    break;
            }

            lvglDisplay.unlock();
            break;
        }
    }
}

const std::string DisplayItem::getComponentType() const {
    return "display_item";
}

void DisplayItem::unregisterTopics() {
    // Clear all current subscriptions using the new clearSubscriptions method
    clearSubscriptions();
}

void DisplayItem::createBox() {
    // Get the main screen from LVGL display
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lv_obj_t* parent = lvglDisplay.getMainScreen();

    if (!parent) {
        ESP_LOGE(TAG, "Main screen not available for DisplayItem %lu", id);
        return;
    }

    // Lock screen access for thread safety
    lvglDisplay.lock();

    // If box already exists, just redraw it with new configuration
    if (box != nullptr) {
        ESP_LOGI(TAG, "Redrawing existing box for DisplayItem %lu", id);
        const int adjustedWidth = width == 0 ? 45 : width;
        draw_box(box, label.c_str(), "", unit.c_str(), x, y, fontSize, adjustedWidth, alignment, boxType);
        lvglDisplay.unlock();
        return;
    }

    // Initialize box styles if not already done (should be done once globally)
    static bool styles_initialized = false;
    if (!styles_initialized) {
        ui_box_create_styles();
        styles_initialized = true;
    }

    // Create the box
    box = ui_box_create(parent);
    if (!box) {
        ESP_LOGE(TAG, "Failed to create UI box for DisplayItem %lu", id);
        lvglDisplay.unlock();
        return;
    }

    // Apply the initial configuration using draw_box
    const int adjustedWidth = width == 0 ? 45 : width;
    draw_box(box, label.c_str(), "", unit.c_str(), x, y, fontSize, adjustedWidth, alignment, boxType);

    lvglDisplay.unlock();
}

void DisplayItem::destroyBox() {
    if (box == nullptr) {
        return;
    }

    // Lock screen access for thread safety
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lvglDisplay.lock();

    lv_obj_del(box);
    box = nullptr;
    ESP_LOGD(TAG, "Destroyed UI box for DisplayItem %lu", id);

    lvglDisplay.unlock();
}

} // freebutton