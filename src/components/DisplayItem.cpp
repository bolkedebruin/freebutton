//
// Created by Bolke de Bruin on 18/11/2025.
//

#include "DisplayItem.h"
#include "mqtt/Registry.h"
#include <esp_log.h>

namespace freebutton {

unsigned long DisplayItem::next_id = 1;

DisplayItem::DisplayItem(int x, int y, int width, int boxtype, int fontSize, int alignment,
                        const std::string& label, const std::string& unit, int round, int page,
                        const std::vector<DisplayTopicConfig>& topicConfigs)
    : id(next_id++)
    , x(x), y(y), width(width), boxType(boxtype)
    , fontSize(fontSize), alignment(alignment)
    , label(label), unit(unit), round(round), page(page)
{
    topics.reserve(topicConfigs.size());
    currentValues.reserve(topicConfigs.size());

    // Convert TopicConfigs to Topics
    for (const auto& config : topicConfigs) {
        auto brokerId = MqttBrokerRegistry::getBrokerId(config.brokerId);
        if (brokerId != 0) {
            topics.emplace_back(Topic{brokerId, config.topic, config.payload, config.eventType});
        }
    }

    registerTopics();
}

void DisplayItem::registerTopics() {
    for (const auto& topic : topics) {
        addSubscription(topic.brokerId, topic.topic);
    }
}

void DisplayItem::onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) {
    for (const auto& topicConfig : topics) {
        if (brokerId == topicConfig.brokerId && topic == topicConfig.topic) {
            currentValues[topicConfig.eventType] = data;
            break;
        }
    }
}

const std::string DisplayItem::getComponentType() const {
    return "display_item";
}

std::string DisplayItem::getDisplayText() const {
    std::string result;
    result.reserve(128);

    result = label;

    if (auto it = currentValues.find(DISPLAY_VALUE); it != currentValues.end()) {
        if (!result.empty()) result += ": ";
        result += it->second;
        if (!unit.empty()) {
            result += " ";
            result += unit;
        }
    }

    if (auto it = currentValues.find(DISPLAY_LABEL); it != currentValues.end()) {
        if (!result.empty()) result += "\n";
        result += it->second;
    }

    return result;
}

} // freebutton