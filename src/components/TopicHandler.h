//
// TopicHandler - Template-based MQTT topic handling
// Created by Bolke de Bruin on 19/11/2025.
//

#ifndef FREEBUTTON_TOPICHANDLER_H
#define FREEBUTTON_TOPICHANDLER_H

#include <vector>
#include "ArduinoJson.h"
#include "Topics.h"
#include "mqtt/Registry.h"

namespace freebutton {

/**
 * Utility class for parsing MQTT topics from JSON
 */
class TopicParser {
public:
    /**
     * Parse topics from JSON array and return vector of valid topics
     * @param topicsArray ArduinoJson array containing topic objects
     * @param componentName Name for logging (e.g., "DisplayItem", "Button")
     * @return Vector of successfully parsed topics
     */
    static std::vector<Topic> parseTopics(const JsonArray& topicsArray,
                                         const char* componentName = "Component");

    /**
     * Validate that a topic configuration is complete and valid
     * @param topic The topic to validate
     * @return true if topic is valid, false otherwise
     */
    static bool isValidTopic(const Topic& topic);

private:
    static constexpr auto TAG = "TopicParser";
};

/**
 * Template-based topic handler using CRTP that automatically handles the common pattern
 * of parsing topics from JSON
 */
template<typename Derived>
class TopicHandler {
public:
    /**
     * Handle topics array from JSON - provides default implementation
     * @param topicsArray ArduinoJson array containing topic objects
     */
    void handleTopics(const JsonArray& topicsArray) {
        auto* derived = static_cast<Derived*>(this);

        // 1. Unregister all current topics first
        derived->unregisterTopics();

        // 2. Parse new topics using the static utility method
        auto parsedTopics = TopicParser::parseTopics(topicsArray, derived->getComponentName());

        // 3. Update topics vector
        auto& topics = derived->getTopicsRef();
        topics = std::move(parsedTopics);

        // 4. Topics will be registered later via ConfigManager::registerAllSubscriptions()
    }
};

} // namespace freebutton

#endif //FREEBUTTON_TOPICHANDLER_H