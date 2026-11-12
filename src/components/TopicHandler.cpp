//
// TopicHandler - Template-based MQTT topic handling
// Created by Bolke de Bruin on 19/11/2025.
//

#include "TopicHandler.h"

namespace freebutton {

std::vector<Topic> TopicParser::parseTopics(const JsonArray& topicsArray,
                                            const char* componentName) {
    std::vector<Topic> topics;
    topics.reserve(topicsArray.size());

    for (const auto& topicElement : topicsArray) {
        if (!topicElement.is<ArduinoJson::JsonObject>()) {
            ESP_LOGW(TAG, "%s: Topic element is not a JSON object, skipping", componentName);
            continue;
        }

        const auto topicObj = topicElement.as<ArduinoJson::JsonObject>();
        Topic topic;

        // Parse the topic from JSON
        topic.fromJson(topicObj);

        // Validate the parsed topic
        if (!isValidTopic(topic)) {
            ESP_LOGW(TAG, "%s: Invalid topic configuration, skipping", componentName);
            continue;
        }

        // Topic is valid, add to vector
        topics.push_back(topic);

        ESP_LOGD(TAG, "%s: Parsed topic - broker: %d, topic: %s, eventType: %d",
                componentName, topic.brokerId, topic.topic.c_str(), static_cast<int>(topic.eventType));
    }

    ESP_LOGI(TAG, "%s: Successfully parsed %zu topics", componentName, topics.size());
    return topics;
}

bool TopicParser::isValidTopic(const Topic& topic) {
    // Check if broker ID is valid (255 means broker not found)
    if (topic.brokerId == 255) {
        ESP_LOGW(TAG, "Topic has invalid broker ID: 255");
        return false;
    }

    // Check if topic string is not empty
    if (topic.topic.empty()) {
        ESP_LOGW(TAG, "Topic string is empty");
        return false;
    }

    // EventType validation - ensure it's within reasonable bounds
    if (topic.eventType > kLastEventType) {
        ESP_LOGW(TAG, "Topic has invalid event type: %d", static_cast<int>(topic.eventType));
        return false;
    }

    return true;
}

} // namespace freebutton