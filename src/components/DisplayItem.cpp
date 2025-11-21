//
// Created by Bolke de Bruin on 18/11/2025.
//

#include "DisplayItem.h"
#include "mqtt/Registry.h"

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
}

void DisplayItem::registerTopics() {
    for (const auto& topic : topics) {
        addSubscription(topic.brokerId, topic.topic);
    }
}

void DisplayItem::onMessage(const uint8_t brokerId, const std::string_view topic, const std::string_view data) {
    for (const auto& topicConfig : topics) {
        if (brokerId == topicConfig.brokerId && topic == topicConfig.topic) {
            ESP_LOGI(TAG, "topic=%s value=%s", topic.data(), data.data());
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

} // freebutton