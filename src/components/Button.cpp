//
// Button - Example button component using TopicHandler
// Created by Bolke de Bruin on 19/11/2025.
//

#include "Button.h"
#include "mqtt/Registry.h"
#include <esp_log.h>

namespace freebutton {

unsigned long Button::next_id = 1;

Button::Button(const std::string& label, const std::string& topLabel,
               uint32_t ledColorFront, uint32_t ledColorWall,
               const std::vector<Topic>& topicConfigs)
    : id(next_id++)
    , label(label), topLabel(topLabel)
    , ledColorFront(ledColorFront), ledColorWall(ledColorWall)
{
    topics.reserve(topicConfigs.size());
    currentValues.reserve(topicConfigs.size());

    // Topics are already in the right format, just copy them
    topics = topicConfigs;

    registerTopics();
}

void Button::registerTopics() {
    for (const auto& topic : topics) {
        // Determine if this is a subscription (incoming) or publication (outgoing) topic
        switch (topic.eventType) {
            case LABEL:
            case TOP_LABEL:
            case LED_RGB:
            case LED_FRONT_RGB:
            case LED_WALL_RGB:
                // These are typically incoming messages to update the button
                addSubscription(topic.brokerId, topic.topic);
                break;

            case CLICK:
            case LONG_PRESS:
            case RELEASE:
                // These are typically outgoing messages when button is pressed
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
    for (const auto& topicConfig : topics) {
        if (brokerId == topicConfig.brokerId && topic == topicConfig.topic) {
            currentValues[topicConfig.eventType] = data;

            // Handle specific message types
            switch (topicConfig.eventType) {
                case LABEL:
                    label = std::string(data);
                    ESP_LOGI(TAG, "Button %lu label updated to: %s", id, label.c_str());
                    break;

                case TOP_LABEL:
                    topLabel = std::string(data);
                    ESP_LOGI(TAG, "Button %lu top label updated to: %s", id, topLabel.c_str());
                    break;

                case LED_FRONT_RGB:
                    // Parse RGB color value (assuming JSON format)
                    ESP_LOGI(TAG, "Button %lu front LED color updated: %s", id, std::string(data).c_str());
                    break;

                case LED_WALL_RGB:
                    // Parse RGB color value (assuming JSON format)
                    ESP_LOGI(TAG, "Button %lu wall LED color updated: %s", id, std::string(data).c_str());
                    break;

                default:
                    ESP_LOGD(TAG, "Button %lu received message for event type %d: %s",
                            id, static_cast<int>(topicConfig.eventType), std::string(data).c_str());
                    break;
            }
            break;
        }
    }
}

const std::string Button::getComponentType() const {
    return "button";
}

void Button::click() {
    publishToTopic(CLICK, CLICK_PAYLOAD);
    ESP_LOGI(TAG, "Button %lu clicked", id);
}

void Button::longPress() {
    publishToTopic(LONG_PRESS, LONG_PRESS_PAYLOAD);
    ESP_LOGI(TAG, "Button %lu long pressed", id);
}

void Button::release() {
    publishToTopic(RELEASE, RELEASE_PAYLOAD);
    ESP_LOGI(TAG, "Button %lu released", id);
}

// handleTopics is now automatically provided by TopicHandler template

void Button::publishToTopic(EventType eventType, const std::string& payload) {
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

} // namespace freebutton