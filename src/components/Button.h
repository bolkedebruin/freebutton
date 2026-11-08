//
// Button - Example button component using TopicHandler
// Created by Bolke de Bruin on 19/11/2025.
//

#ifndef FREEBUTTON_BUTTON_H
#define FREEBUTTON_BUTTON_H

#include "mqtt/Component.h"
#include "types.h"
#include "TopicHandler.h"
#include "ButtonEventPlan.h"
#include "ui/PendingRender.h"
#include "utils/TextSlot.h"
#include <string>
#include <vector>
#include <memory>

#include "ArduinoJson/Object/JsonObject.hpp"
#include "json/JsonWriter.h"

extern "C" {
#include "ui/ui_button.h"
}

namespace freebutton {

class Button final : public MqttSubscriber, public MqttPublisher, public TopicHandler<Button>, public ui::PendingRender {
public:
    static unsigned long next_id;
    unsigned long id = 0;

    std::string label;
    std::string topLabel;
    uint32_t ledColorFront;
    uint32_t ledColorWall;

    Button() : Button("", "", 0, 0, {}) {}

    Button(std::string  label, std::string  topLabel,
           uint32_t ledColorFront, uint32_t ledColorWall,
           std::vector<Topic> topicConfigs);

    ~Button();

private:
    lv_obj_t* button = nullptr;  // UI button element; written only with the LVGL lock held

    // Label / top label set from other tasks (MQTT, JS), rendered by the LVGL
    // task in applyPending(). 32 B footprint each (26 text bytes, see
    // TextSlot.h): the mini panel is 160 px wide; the label (32 px font,
    // wraps) shows ~9 glyphs per line and the top label (18 px) ~16, so 26
    // bytes covers two lines of ASCII label or a full top label (an MDI icon
    // is 4 bytes). lv_label_set_text() does not parse colour escapes, so
    // there is no escape overhead here.
    static constexpr size_t kLabelSlotBytes = 32;
    static constexpr size_t kTopLabelSlotBytes = 32;
    TextSlot<kLabelSlotBytes> labelSlot_;
    TextSlot<kTopLabelSlotBytes> topLabelSlot_;

    void invalidatePending();

    // Button position and device information
    uint32_t position_{0};
    uint8_t page_{1};
    uint8_t brokerId_{0};

public:

    // MqttComponent interface
    void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) override;
    const std::string getComponentType() const override;

    // What the button shows: the text last set at runtime (MQTT, JS), else
    // the configured label. Any task. out must hold kLabelTextBytes.
    static constexpr size_t kLabelTextBytes = TextSlot<kLabelSlotBytes>::kBufferSize;
    static_assert(kLabelSlotBytes == kTopLabelSlotBytes, "label getters share one buffer size");
    void currentLabel(char (&out)[kLabelTextBytes]) const;
    void currentTopLabel(char (&out)[kLabelTextBytes]) const;

    // Button-specific methods
    void click() const;
    void longPress() const;
    void release() const;

    // Position and event handling
    uint32_t getPosition() const { return position_; }
    void handlePhysicalEvent(EventType event);

    // How this key is exposed to Home Assistant (event entity, event_types,
    // canonical-topic mirroring). Shared by discovery and the press publisher.
    ha::ButtonEventPlan homeAssistantPlan(uint8_t haBrokerId) const;

    // UI button access for scripting
    lv_obj_t* getButtonObject() const { return button; }

    // Programmatic updates (JS). Any task: they only store the text; the
    // LVGL task renders it on its next tick. No LVGL lock is taken.
    void setLabelText(std::string_view text);
    void setTopLabelText(std::string_view text);

    // PendingRender (LVGL task, LVGL lock held)
    bool applyPending() override;

    static std::unique_ptr<Button> fromJson(const JsonObject& config) {
        auto button = std::make_unique<Button>();
        button->configure(config);
        button->createButton();   // Create UI button after configuration
        return button;
    }

    template<typename OutputStream>
    void toJson(json::JsonWriter<OutputStream>::ScopedObject &obj) {
        obj.Add("id", id);
        obj.Add("position", position_);
        obj.Add("buttonuid", std::to_string(position_) + "-" + std::to_string(page_)); // not used
        obj.Add("page", page_);
        obj.Add("label", label);
        obj.Add("topLabel", topLabel);

        // topics array
        {
            auto arr = obj.Array("topics");
            for (auto &topic: topics) {
                auto item = arr.Object();
                // tp.second.eventType = tp.first;
                topic.toJson<OutputStream>(item);
            }
        }
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

        // ignoring buttonid not used in freebutton
        data["position"].isNull() ? position_ = id : position_ = data["position"].as<uint32_t>();
        data["page"].isNull() ? page_ = 1 : page_ = data["page"].as<uint32_t>();

        label = data["label"].isNull() ? label : data["label"].as<std::string>();
        topLabel = data["toplabel"].isNull() ? topLabel : data["toplabel"].as<std::string>();
        ledColorFront = data["ledcolorfront"].isNull() ? ledColorFront : data["ledcolorfront"].as<uint32_t>();
        ledColorWall = data["ledcolorwall"].isNull() ? ledColorWall : data["ledcolorwall"].as<uint32_t>();

        // Set device ID and broker ID - use external deviceId from main.cpp
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

        ESP_LOGI(TAG, "Configured Button %d at position %d", id, position_);
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