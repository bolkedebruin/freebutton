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
#include <memory>

#include "ArduinoJson/Object/JsonObject.hpp"

extern "C" {
#include "ui/ui_button.h"
}

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

    ~Button();

private:
    lv_obj_t* button = nullptr;  // UI button element


    // Button position and device information
    uint32_t position_{0};
    uint8_t page_{1};
    std::string buttonId_;
    std::string deviceId_;
    uint8_t brokerId_{0};

public:

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
        button->createButton();   // Create UI button after configuration
        return button;
    }

private:
    static constexpr auto TAG = "Button";
    std::vector<Topic> topics;

    void publishToTopic(EventType eventType, const std::string& payload) const;

    // Configuration from JSON
    void configure(const JsonObject& data) {
        // Legacy fields for backwards compatibility
        if (!data["id"].isNull()) {
            id = data["id"].as<unsigned long>();
        }

        // Parse basic button properties (support both old and new format)
        if (!data["buttonid"].isNull()) {
            // New format: buttonid, position, page
            buttonId_ = data["buttonid"].as<std::string>();
            position_ = data["position"].as<uint32_t>();
            page_ = data["page"].as<uint32_t>();
        } else if (!data["id"].isNull()) {
            // Legacy format: use id as both buttonId and position
            buttonId_ = std::to_string(data["id"].as<uint32_t>());
            position_ = data["id"].as<uint32_t>();
            page_ = 1; // Default page for legacy format
        } else {
            // Fallback
            buttonId_ = "unknown";
            position_ = 0;
            page_ = 1;
        }

        label = data["label"].isNull() ? label : data["label"].as<std::string>();
        topLabel = data["toplabel"].isNull() ? topLabel : data["toplabel"].as<std::string>();
        ledColorFront = data["ledcolorfront"].isNull() ? ledColorFront : data["ledcolorfront"].as<uint32_t>();
        ledColorWall = data["ledcolorwall"].isNull() ? ledColorWall : data["ledcolorwall"].as<uint32_t>();

        // Set device ID and broker ID - use external deviceId from main.cpp
        deviceId_ = deviceId;
        brokerId_ = 0; // Default broker ID

        // Parse LED topics for backwards compatibility (but don't create LEDs here)
        if (data["leds"].is<JsonArray>()) {
            for (const auto& ledJson : data["leds"].as<JsonArray>()) {
                // Add LED topics to main topics list (handled by TopicHandler)
                if (ledJson["topics"].is<JsonArray>()) {
                    handleTopics(ledJson["topics"].as<JsonArray>());  // Existing TopicHandler logic
                }
            }
        }

        // Parse button topics (existing logic)
        if (data["topics"].is<JsonArray>()) {
            handleTopics(data["topics"].as<JsonArray>());
        } else {
            ESP_LOGW(TAG, "No topics in Button JSON");
        }

        ESP_LOGI(TAG, "Configured Button %s at position %d", buttonId_.c_str(), position_);
    }

public:
    // CRTP interface for TopicHandler
    static const char* getComponentName() { return "Button"; }
    std::vector<Topic>& getTopicsRef() { return topics; }
    void registerTopics();
    void unregisterTopics();

    // UI methods
    void createButton();
    void destroyButton();


};

} // namespace freebutton

#endif //FREEBUTTON_BUTTON_H