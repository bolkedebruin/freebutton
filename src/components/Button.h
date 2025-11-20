//
// Button - Example button component using TopicHandler
// Created by Bolke de Bruin on 19/11/2025.
//

#ifndef FREEBUTTON_BUTTON_H
#define FREEBUTTON_BUTTON_H

#include "mqtt/Component.h"
#include "types.h"
#include "TopicHandler.h"
#include <string>
#include <vector>

#include "ArduinoJson/Object/JsonObject.hpp"

namespace freebutton {

class Button final : public MqttSubscriber, public MqttPublisher, public TopicHandler<Button> {
public:
    static unsigned long next_id;

    unsigned long id;
    std::string label;
    std::string topLabel;
    uint32_t ledColorFront;
    uint32_t ledColorWall;

    Button() : Button("default", "", 0, 0, {}) {}

    Button(std::string  label, std::string  topLabel,
           uint32_t ledColorFront, uint32_t ledColorWall,
           std::vector<Topic> topicConfigs);

    // MqttComponent interface
    void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) override;
    const std::string getComponentType() const override;

    // Button-specific methods
    void click() const;
    void longPress() const;
    void release() const;

    static std::unique_ptr<Button> fromJson(const JsonObject& config) {
        auto button = std::make_unique<Button>();
        button->configure(config);
        return button;
    }

private:
    static constexpr auto TAG = "Button";
    std::vector<Topic> topics;

    void publishToTopic(EventType eventType, const std::string& payload) const;

    // Configuration from JSON
    void configure(const ArduinoJson::JsonObject& data) {
        label = data["label"].isNull() ? label : data["label"].as<std::string>();
        topLabel = data["toplabel"].isNull() ? topLabel : data["toplabel"].as<std::string>();
        ledColorFront = data["ledcolorfront"].isNull() ? ledColorFront : data["ledcolorfront"].as<uint32_t>();
        ledColorWall = data["ledcolorwall"].isNull() ? ledColorWall : data["ledcolorwall"].as<uint32_t>();

        if (data["topics"].is<ArduinoJson::JsonArray>()) {
            handleTopics(data["topics"].as<ArduinoJson::JsonArray>());
        } else {
            ESP_LOGW(TAG, "No topics in Button JSON");
        }
    }

public:
    // CRTP interface for TopicHandler
    const char* getComponentName() const { return "Button"; }
    std::vector<Topic>& getTopicsRef() { return topics; }
    void registerTopics();
    void unregisterTopics();

};

} // namespace freebutton

#endif //FREEBUTTON_BUTTON_H