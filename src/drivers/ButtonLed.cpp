//
// ButtonLed - Implementation of lightweight LED controller
// Created by Claude Code on 25/11/2025.
//

#include "ButtonLed.h"
#include "NeoPixel.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include "mqtt/Component.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include "config.h"

static auto TAG = "led";

void ButtonLed::updateDisplay() const noexcept {
    if (state_.on) {
        const uint8_t scaledR = (state_.red * state_.brightness) / 255;
        const uint8_t scaledG = (state_.green * state_.brightness) / 255;
        const uint8_t scaledB = (state_.blue * state_.brightness) / 255;

        neopixels.setPixel(ledIndex_,
                          NeoPixelController::colorFromRGB(scaledR, scaledG, scaledB));
    } else {
        neopixels.setPixel(ledIndex_, CRGB::Black);
    }
    NeoPixelController::show();
}

// Public methods for state retrieval (used by Button component for publishing)
std::string ButtonLed::getStateJson() const {
    return stateToJson();
}

std::string ButtonLed::getBrightnessString() const {
    return std::to_string(state_.brightness);
}

std::string ButtonLed::getColorJson() const {
    return colorToJson();
}

std::string ButtonLed::getEffectString() const {
    return state_.effect;
}

std::string ButtonLed::generateBaseTopic() const noexcept {
    const std::string buttonId = std::to_string(buttonPosition_);
    const std::string ledSuffix = (ledPosition_ == LedPosition::BOTH) ?
        ("_" + std::to_string(ledNumber_)) :
        ("_" + std::string(positionToString()));

    return std::string(PUBLICMQTTID) + "/" + deviceId_ + "/button" + buttonId + ledSuffix;
}

std::string ButtonLed::getStateTopic() const noexcept {
    return generateBaseTopic() + "/led/state";
}

std::string ButtonLed::getCommandTopic() const noexcept {
    return generateBaseTopic() + "/led/set";
}

std::string ButtonLed::getBrightnessStateTopic() const noexcept {
    return generateBaseTopic() + "/led/brightness/state";
}

std::string ButtonLed::getBrightnessCommandTopic() const noexcept {
    return generateBaseTopic() + "/led/brightness/set";
}

std::string ButtonLed::getColorStateTopic() const noexcept {
    return generateBaseTopic() + "/led/color/state";
}

std::string ButtonLed::getColorCommandTopic() const noexcept {
    return generateBaseTopic() + "/led/color/set";
}

std::string ButtonLed::getEffectStateTopic() const noexcept {
    return generateBaseTopic() + "/led/effect/state";
}

std::string ButtonLed::getEffectCommandTopic() const noexcept {
    return generateBaseTopic() + "/led/effect/set";
}

std::string ButtonLed::stateToJson() const {
    JsonDocument doc;
    doc["state"] = state_.on ? "ON" : "OFF";
    doc["brightness"] = state_.brightness;
    doc["color"]["r"] = state_.red;
    doc["color"]["g"] = state_.green;
    doc["color"]["b"] = state_.blue;
    doc["effect"] = state_.effect;

    std::string result;
    serializeJson(doc, result);
    return result;
}

std::string ButtonLed::colorToJson() const {
    JsonDocument doc;
    doc["r"] = state_.red;
    doc["g"] = state_.green;
    doc["b"] = state_.blue;

    std::string result;
    serializeJson(doc, result);
    return result;
}

void ButtonLed::registerWithHomeAssistant() const {
    // Convert to callback-based registration for memory efficiency
    auto generator = [this](uint8_t broker_id) -> HomeAssistantDiscoveryConfig {
        HomeAssistantDiscoveryConfig config;
        config.component_type = "light";
        config.default_entity_id = getObjectId();  // "freebutton01_7_1"
        config.name = getEntityName();           // "Button 7 Wall LED"

        // Core light functionality - runtime topic generation
        config.state_topic = getStateTopic();
        config.command_topic = getCommandTopic();
        config.additional_config["schema"] = "json";
        config.additional_config["retain"] = "true";

        // RGB color support - runtime topic generation
        config.additional_config["rgb_state_topic"] = getColorStateTopic();
        config.additional_config["rgb_command_topic"] = getColorCommandTopic();
        config.additional_config["rgb_command_template"] =
            R"({{ {"r": red, "g": green, "b": blue} | tojson }})";
        config.additional_config["rgb_value_template"] =
            "{{ value_json.r }},{{ value_json.g }},{{ value_json.b }}";

        // Brightness support (0-255 scale) - runtime topic generation
        config.additional_config["brightness_state_topic"] = getBrightnessStateTopic();
        config.additional_config["brightness_command_topic"] = getBrightnessCommandTopic();
        config.additional_config["brightness_scale"] = "255";

        // Effect support - runtime topic generation
        config.additional_config["effect_state_topic"] = getEffectStateTopic();
        config.additional_config["effect_command_topic"] = getEffectCommandTopic();
        config.additional_config["effect_list"] = R"(["none","glow","flash"])";

        config.device = HomeAssistantDiscovery::getGlobalDeviceInfo();
        config.enabled = true;
        return config;
    };

    HomeAssistantDiscovery::registerComponent(brokerId_, std::move(generator));

    ESP_LOGI(TAG, "Registered %s with Home Assistant (callback)", getEntityName().c_str());
}

bool ButtonLed::matchesTopic(std::string_view topic) const noexcept {
    const std::string baseTopic = generateBaseTopic();
    return topic.find(baseTopic) == 0;
}

void ButtonLed::handleMqttMessage(const std::string_view topic, std::string_view data) {
    const std::string topicStr(topic);

    if (topicStr == getCommandTopic()) {
        // Handle JSON state command
        JsonDocument doc;
        if (deserializeJson(doc, data) == DeserializationError::Ok) {
            if (!doc["state"].isNull()) {
                setState(doc["state"] == "ON");
            }
            if (!doc["brightness"].isNull()) {
                setBrightness(doc["brightness"].as<uint8_t>());
            }
            if (!doc["color"].isNull()) {
                setColor(doc["color"]["r"], doc["color"]["g"], doc["color"]["b"]);
            }
            if (!doc["effect"].isNull()) {
                setEffect(doc["effect"].as<std::string>());
            }
        }
    } else if (topicStr == getBrightnessCommandTopic()) {
        setBrightness(std::stoi(std::string(data)));
    } else if (topicStr == getColorCommandTopic()) {
        JsonDocument doc;
        if (deserializeJson(doc, data) == DeserializationError::Ok) {
            setColor(doc["r"], doc["g"], doc["b"]);
        }
    } else if (topicStr == getEffectCommandTopic()) {
        setEffect(std::string(data));
    }
}