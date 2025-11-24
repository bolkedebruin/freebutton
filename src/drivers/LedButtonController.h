//
// LedButtonController - Integrates PushButton with NeoPixel LEDs and MQTT control
// Created by Bolke de Bruin on 31/08/2025.
//

#pragma once

#include "PushButton.h"
#include "NeoPixel.h"
#include "mqtt/Component.h"
#include <memory>
#include <ArduinoJson.h>

namespace FreeButton {

class LedButtonController : public MqttSubscriber, public MqttPublisher {
public:
    struct LedState {
        bool on = false;
        uint8_t brightness = 255;  // 0-255
        uint8_t red = 255;
        uint8_t green = 255;
        uint8_t blue = 255;
        std::string effect = "none";  // "none", "glow", "flash"
    };

    explicit LedButtonController(std::shared_ptr<PushButton> button,
                                std::shared_ptr<NeoPixelController> neopixel,
                                uint8_t brokerId,
                                const std::string& deviceId);
    ~LedButtonController() = default;

    // Non-copyable, movable
    LedButtonController(const LedButtonController&) = delete;
    LedButtonController& operator=(const LedButtonController&) = delete;
    LedButtonController(LedButtonController&&) = default;
    LedButtonController& operator=(LedButtonController&&) = default;

    // MqttComponent interface
    [[nodiscard]] const std::string getComponentType() const override;
    void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) override;

    // LED control methods
    void setLedState(bool on);
    void setLedColor(uint8_t red, uint8_t green, uint8_t blue);
    void setLedBrightness(uint8_t brightness);
    void setLedEffect(const std::string& effect);

    // Get current LED state
    [[nodiscard]] const LedState& getLedState() const { return ledState_; }
    
    // Get button position (1-based)
    [[nodiscard]] int getPosition() const { return button_->position(); }

    // Update LED display
    void updateLedDisplay();

    // Publish current state to MQTT
    void publishState();

    // Home Assistant Discovery
    void registerWithHomeAssistant() const;

private:
    static constexpr const char* TAG = "LedButtonController";
    
    std::shared_ptr<PushButton> button_;
    std::shared_ptr<NeoPixelController> neopixel_;
    uint8_t brokerId_;
    std::string deviceId_;
    LedState ledState_;
    
    // MQTT topics
    std::string stateTopic_;
    std::string commandTopic_;
    std::string brightnessStateTopic_;
    std::string brightnessCommandTopic_;
    std::string colorStateTopic_;
    std::string colorCommandTopic_;
    std::string effectStateTopic_;
    std::string effectCommandTopic_;

    void setupMqttTopics();
    void handleStateCommand(const JsonDocument& json);
    void handleBrightnessCommand(const std::string& data);
    void handleColorCommand(const JsonDocument& json);
    void handleEffectCommand(const std::string& data);
    
    // Calculate LED index based on button position
    [[nodiscard]] int getLedIndex() const;
    
    // Convert state to JSON for publishing
    [[nodiscard]] std::string stateToJson() const;
    [[nodiscard]] std::string colorToJson() const;
};

} // namespace FreeButton